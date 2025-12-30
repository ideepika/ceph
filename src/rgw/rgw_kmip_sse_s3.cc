// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab ft=cpp

#include "rgw_kmip_sse_s3.h"
#include "rgw_kmip_client_impl.h"
#include "common/errno.h"
#include "common/async/yield_context.h"

extern "C" {
#include "kmip.h"
#include "kmip_bio.h"
}

#include <openssl/rand.h>

#define dout_subsys ceph_subsys_rgw

// Global singleton
static RGWKmipSSES3* g_kmip_sse_s3_backend = nullptr;
static ceph::mutex g_kmip_sse_s3_lock = ceph::make_mutex("kmip_sse_s3");

RGWKmipSSES3::RGWKmipSSES3(CephContext* cct) 
  : cct(cct) {
}

RGWKmipSSES3::~RGWKmipSSES3() {
  if (rgw_kmip_manager) {
    rgw_kmip_manager->stop();
  }
}

int RGWKmipSSES3::initialize() {
  rgw_kmip_manager = new RGWKMIPManagerImpl(cct);
  if (!rgw_kmip_manager) {
    ldout(cct, 0) << "ERROR: Failed to create KMIP manager" << dendl;
    return -ENOMEM;
  }
  
  int ret = rgw_kmip_manager->start();
  if (ret < 0) {
    ldout(cct, 0) << "ERROR: Failed to start KMIP manager: " 
                  << cpp_strerror(ret) << dendl;
    delete rgw_kmip_manager;
    rgw_kmip_manager = nullptr;
    return ret;
  }
  
  ldout(cct, 10) << "KMIP SSE-S3 backend initialized" << dendl;
  return 0;
}

int RGWKmipSSES3::create_bucket_key(const DoutPrefixProvider* dpp,
                                     const std::string& bucket_name,
                                     std::string& kek_id_out) {
  ldpp_dout(dpp, 20) << "Creating KEK for bucket: " << bucket_name << dendl;

  struct CreateAndActivateKey : public RGWKMIPTransceiver {
    std::string kek_id;
    std::string name;
    const DoutPrefixProvider* dpp;

    CreateAndActivateKey(CephContext* cct, const std::string& name_in, const DoutPrefixProvider* dpp_in)
      : RGWKMIPTransceiver(cct, RGWKMIPTransceiver::CREATE),
        name(name_in), 
        dpp(dpp_in) {}
    
    int execute(KMIP* ctx, BIO* bio) override {
      ldpp_dout(this->dpp, 0) << "KMIP execute ENTRY" << dendl;
      char* key_id = nullptr;
      int key_id_size = 0;

      TemplateAttribute template_attr = {0};
      Attribute attrs[3];
      memset(attrs, 0, sizeof(attrs));
      int attr_count = 0;
      
      // Attr 1: AES
      attrs[0].type = KMIP_ATTR_CRYPTOGRAPHIC_ALGORITHM;
      attrs[0].value = ctx->calloc_func(ctx->state, 1, sizeof(int32));
      *(int32*)attrs[0].value = KMIP_CRYPTOALG_AES;

      // Attr 2: 256 bits
      attrs[1].type = KMIP_ATTR_CRYPTOGRAPHIC_LENGTH;
      attrs[1].value = ctx->calloc_func(ctx->state, 1, sizeof(int32));
      *(int32*)attrs[1].value = 256;

      // Attr 3: Encrypt+Decrypt
      attrs[2].type = KMIP_ATTR_CRYPTOGRAPHIC_USAGE_MASK;
      attrs[2].value = ctx->calloc_func(ctx->state, 1, sizeof(int32));
      *(int32*)attrs[2].value = KMIP_CRYPTOMASK_ENCRYPT | KMIP_CRYPTOMASK_DECRYPT;

      attr_count = 3;
      template_attr.attributes = attrs;
      template_attr.attribute_count = attr_count;

      ldpp_dout(this->dpp, 0) << "KMIP_EXECUTE_MARKER: Calling kmip_bio_create..." << dendl;
      int ret = kmip_bio_create_symmetric_key_with_context(ctx, bio, &template_attr, &key_id, &key_id_size);
      ldpp_dout(this->dpp, 0) << "KMIP_EXECUTE_MARKER: Create returned: " << ret 
                              << " Pointer: " << (void*)key_id 
                              << " Size: " << key_id_size << dendl;

      if (ret != KMIP_OK || !key_id) {
        ldpp_dout(this->dpp, 0) << "KMIP create failed: " << ret << dendl;
        for (int i = 0; i < attr_count; i++) {
          if (attrs[i].value) ctx->free_func(ctx->state, attrs[i].value);
        }
        return -EIO;
      }

      kek_id = std::string(key_id, key_id_size);
      ldpp_dout(this->dpp, 0) << "SUCCESS: KMIP created key. kek_id=" << kek_id << dendl;

      // Activate
      ret = kmip_bio_activate_with_context(ctx, bio, key_id);
      kmip_free_buffer(ctx, key_id, key_id_size);
      
      // Free attrs
      for (int i = 0; i < attr_count; i++) {
        if (attrs[i].value) ctx->free_func(ctx->state, attrs[i].value);
      }

      if (ret != KMIP_OK) {
        ldpp_dout(this->dpp, 0) << "KMIP activate failed: " << ret << dendl;
        return -EIO;
      }

      return 0;
    }
  };  // ← END OF STRUCT

  // NOW OUTSIDE STRUCT - Function continues normally
  ldpp_dout(dpp, 0) << "=== KMIP DEBUG ===" << dendl;
  ldpp_dout(dpp, 0) << "rgw_kmip_manager=" << (void*)rgw_kmip_manager << dendl;
  ldpp_dout(dpp, 0) << "dpp->get_cct()=" << (void*)dpp->get_cct() << dendl;

  if (!rgw_kmip_manager) {
    ldpp_dout(dpp, 0) << "FATAL: rgw_kmip_manager NULL!" << dendl;
    return -EINVAL;
  }

  const std::string key_template = "pykmip-" + bucket_name;
  ldpp_dout(dpp, 0) << "key_template=" << key_template << dendl;

  CreateAndActivateKey op(dpp->get_cct(), key_template, dpp);
  ldpp_dout(dpp, 0) << "&op=" << (void*)&op << dendl;

  int ret = rgw_kmip_manager->add_request(&op);
  ldpp_dout(dpp, 0) << "add_request() RETURNS: " << ret << dendl;

  if (ret < 0) {
    ldpp_dout(dpp, 0) << "add_request FAILED: " << ret << dendl;
    return ret;
  }

  ret = op.wait(dpp, optional_yield(optional_yield::empty_t{}));
  if (ret < 0) {
    ldpp_dout(dpp, 0) << "Create KEK failed" << dendl;
    return ret;
  }

  kek_id_out = op.kek_id;
  ldpp_dout(dpp, 10) << "Created KEK UUID: " << kek_id_out << dendl;
  return 0;
}

int RGWKmipSSES3::destroy_bucket_key(const DoutPrefixProvider* dpp,
                                      const std::string& kek_id) {
  ldpp_dout(dpp, 10) << "Destroying KEK: " << kek_id << dendl;
  
  struct DestroyKey : public RGWKMIPTransceiver {
    const std::string& kek_id;
    
   DestroyKey(CephContext* cct, const std::string& kek)
  : RGWKMIPTransceiver(cct, RGWKMIPTransceiver::DESTROY), 
    kek_id(kek) {}

    
    int execute(KMIP* ctx, BIO* bio)  override {
      int ret = kmip_bio_destroy_symmetric_key_with_context(
            ctx, bio, const_cast<char*>(kek_id.c_str()), static_cast<int>(kek_id.length())
      );
      return (ret == KMIP_OK) ? 0 : -EIO;
    }
  };
  
  DestroyKey op(dpp->get_cct(), kek_id);
  
  int ret = rgw_kmip_manager->add_request(&op);
  ldpp_dout(dpp, 0) << "add_request ret=" << ret << dendl;
  
  if (ret < 0) return ret;
  
  ret = op.wait(dpp, optional_yield(optional_yield::empty_t{}));
  if (ret < 0) {
    ldpp_dout(dpp, 0) << "Destroy KEK failed" << dendl;
  }
  return ret;
}

int RGWKmipSSES3::generate_and_wrap_dek(const DoutPrefixProvider* dpp,
                                         const std::string& kek_id,
                                         const std::string& encryption_context,
                                         bufferlist& plaintext_dek_out,
                                         bufferlist& wrapped_dek_out) {
  // Generate random DEK
  unsigned char dek[32];
  if (RAND_bytes(dek, 32) != 1) {
    ldpp_dout(dpp, 0) << "Failed to generate DEK" << dendl;
    return -EIO;
  }
  
  plaintext_dek_out.append((char*)dek, 32);
  ldpp_dout(dpp, 10) << "Wrapping DEK with KEK: " << kek_id << dendl;
  
  // Wrap DEK with KMIP
  struct WrapDEK : public RGWKMIPTransceiver {
    const std::string& kek_id;
    const unsigned char* dek_ptr;
    bufferlist wrapped_dek;
    const DoutPrefixProvider* dpp;
   
    WrapDEK(CephContext* cct, const std::string& kek, const unsigned char* dek_ptr, const DoutPrefixProvider* dpp_in)
  : RGWKMIPTransceiver(cct, RGWKMIPTransceiver::ENCRYPT), 
    kek_id(kek),
    dek_ptr(dek_ptr),
    dpp(dpp_in) {}

    
    int execute(KMIP* ctx, BIO* bio)  override {
        // Set up cryptographic parameters
        CryptographicParameters params;
        memset(&params, 0, sizeof(params));
        kmip_init_cryptographic_parameters(&params);
        params.cryptographic_algorithm = KMIP_CRYPTOALG_AES;
        params.block_cipher_mode = KMIP_BLOCK_CBC;
        params.padding_method = KMIP_PAD_PKCS5;
        params.random_iv = KMIP_TRUE;  // Server generates IV

        uint8* ciphertext = nullptr;
        int ciphertext_size = 0;
        uint8* iv = nullptr;
        int iv_size = 0;
        ldpp_dout(dpp, 0) << "kmip debug: encrypt execute " << dendl;
      int ret = kmip_bio_encrypt_with_context(
        ctx, bio,
        const_cast<char*>(kek_id.c_str()),  // key_id
        kek_id.length(),                     // key_id_size
        const_cast<uint8*>(dek_ptr),        // plaintext
        32,                                  // plaintext_size (256-bit DEK)
        &params,                             // crypto params
        &ciphertext,                         // ciphertext out
        &ciphertext_size,                    // ciphertext size out
        &iv,                                 // IV out
        &iv_size                              // IV size out
        );
      
      if (ret != KMIP_OK) {
        ldpp_dout(dpp, 0) << "KMIP encrypt failed" << dendl;
        return -EIO;
      }
      
      // Package: [IV_SIZE(4 bytes)][IV][CIPHERTEXT]
      uint32_t iv_sz = iv_size;
      wrapped_dek.append((char*)&iv_sz, sizeof(iv_sz));
      wrapped_dek.append((char*)iv, iv_size);
      wrapped_dek.append((char*)ciphertext, ciphertext_size);
      
      if (ciphertext) free(ciphertext);
      if (iv) free(iv);
        
        return ret;
    }
  };
  
  WrapDEK op(cct, kek_id, dek, dpp);
  
  int ret = rgw_kmip_manager->add_request(&op);
  ldpp_dout(dpp, 0) << "add_request ret=" << ret << dendl;
  
  // Always zero sensitive data
  explicit_bzero(dek, 32);
  
  if (ret < 0) return ret;
  
  ret = op.wait(dpp, optional_yield(optional_yield::empty_t{}));
  if (ret < 0) {
    ldpp_dout(dpp, 0) << "Wrap DEK failed" << dendl;
    return ret;
  }
  
  wrapped_dek_out = std::move(op.wrapped_dek);
  ldpp_dout(dpp, 10) << "Successfully wrapped DEK" << dendl;
  return 0;
}

int RGWKmipSSES3::unwrap_dek(const DoutPrefixProvider* dpp,
                              const std::string& kek_id,
                              const bufferlist& wrapped_dek,
                              const std::string& encryption_context,
                              bufferlist& plaintext_dek_out) {
  ldpp_dout(dpp, 10) << "kmip debug: Unwrapping DEK with KEK: " << kek_id << dendl;
  
  struct UnwrapDEK : public RGWKMIPTransceiver {
    const std::string& kek_id;
    bufferlist wrapped_dek;
    bufferlist plaintext_dek;
    const DoutPrefixProvider* dpp;
    
    UnwrapDEK(CephContext *cct, const std::string& kek, const bufferlist& wrapped, const DoutPrefixProvider* dpp_in)
    : RGWKMIPTransceiver(cct, RGWKMIPTransceiver::DECRYPT),
      kek_id(kek),
      wrapped_dek(wrapped),
      dpp(dpp_in) {}
    
    int execute(KMIP* ctx, BIO* bio)  override {
      // 1. Unpack wrapped DEK: [IV_SIZE][IV][CIPHERTEXT]
      const char* data = wrapped_dek.c_str();
      uint32_t iv_size;
      memcpy(&iv_size, data, sizeof(iv_size));
      
      const uint8* iv = (uint8*)(data + sizeof(iv_size));
      const uint8* ciphertext = iv + iv_size;
      size_t ct_size = wrapped_dek.length() - sizeof(iv_size) - iv_size;
      
      // Set up cryptographic parameters
      CryptographicParameters params;
      memset(&params, 0, sizeof(params));
      kmip_init_cryptographic_parameters(&params);
      params.cryptographic_algorithm = KMIP_CRYPTOALG_AES;
      params.block_cipher_mode = KMIP_BLOCK_CBC;
      params.padding_method = KMIP_PAD_PKCS5;
      params.random_iv = KMIP_TRUE;
        
      uint8* plaintext = nullptr;
      int32 plaintext_size = 0;
      
      // Decrypt with KMIP
      int ret = kmip_bio_decrypt_with_context(
      ctx, bio,
      const_cast<char*>(kek_id.c_str()),     // key_id
      kek_id.length(),                        // key_id_size
      const_cast<uint8*>(ciphertext),        // ciphertext
      ct_size,                                // ciphertext_size
      const_cast<uint8*>(iv),                // IV
      iv_size,                                // IV_size
      &params,                                // crypto params
      &plaintext,                             // plaintext out
      &plaintext_size                         // plaintext size out
    );
      
      if (ret != KMIP_OK) {
        ldpp_dout(dpp, 0) << "KMIP decrypt failed" << dendl;
        return -EIO;
      }
      
      plaintext_dek.append((char*)plaintext, plaintext_size);
      kmip_free_buffer(ctx, plaintext, plaintext_size);
      return 0;
    }
  };
  
  UnwrapDEK op(cct, kek_id, wrapped_dek, dpp);
  
  int ret = rgw_kmip_manager->add_request(&op);
  if (ret < 0) return ret;
  
  ret = op.wait(dpp, optional_yield(optional_yield::empty_t{}));
  if (ret < 0) {
    ldpp_dout(dpp, 0) << "Unwrap DEK failed" << dendl;
    return ret;
  }
  
  plaintext_dek_out = std::move(op.plaintext_dek);
  ldpp_dout(dpp, 10) << "Successfully unwrapped DEK" << dendl;
  return 0;
}

RGWKmipSSES3* get_kmip_sse_s3_backend(CephContext* cct) {
  std::unique_lock l{g_kmip_sse_s3_lock};
  
  if (!g_kmip_sse_s3_backend) {
    g_kmip_sse_s3_backend = new RGWKmipSSES3(cct);
    int ret = g_kmip_sse_s3_backend->initialize();
    if (ret < 0) {
      ldout(cct, 0) << "Failed to initialize KMIP SSE-S3 backend" << dendl;
      delete g_kmip_sse_s3_backend;
      g_kmip_sse_s3_backend = nullptr;
    }
  }
  return g_kmip_sse_s3_backend;
}

void cleanup_kmip_sse_s3_backend() {
  std::unique_lock l{g_kmip_sse_s3_lock};
  if (g_kmip_sse_s3_backend) {
    delete g_kmip_sse_s3_backend;
    g_kmip_sse_s3_backend = nullptr;
  }
}
