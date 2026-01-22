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
#include "include/buffer.h"

#define dout_subsys ceph_subsys_rgw

static std::string hex_dump(const uint8_t* data, size_t len, size_t max_display = 32) {
  std::ostringstream oss;
  size_t display = std::min(len, max_display);
  for (size_t i = 0; i < display; i++) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02x", data[i]);
    oss << buf;
  }
  if (len > max_display) oss << "...";
  return oss.str();
}

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
                                     std::string& kek_id_out,
                                     optional_yield y) {
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
      ldpp_dout(this->dpp, 10) << "KMIP execute ENTRY" << dendl;
      kmip_init(ctx, NULL, 0, KMIP_1_4);

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

      int ret = kmip_bio_create_symmetric_key_with_context(ctx, bio, &template_attr, &key_id, &key_id_size);
      ldpp_dout(this->dpp, 10) << "KMIP_EXECUTE_MARKER: Create returned: " << ret
                              << " Pointer: " << (void*)key_id
                              << " Size: " << key_id_size << dendl;

      if (ret != KMIP_OK || !key_id) {
        ldpp_dout(this->dpp, 5) << "KMIP create failed: " << ret << dendl;
        for (int i = 0; i < attr_count; i++) {
          if (attrs[i].value) ctx->free_func(ctx->state, attrs[i].value);
        }
        return -EIO;
      }

      kek_id = std::string(key_id, key_id_size);
      ldpp_dout(this->dpp, 5) << "SUCCESS: KMIP created key. kek_id=" << kek_id << dendl;

      // Activate
      ret = kmip_bio_activate_with_context(ctx, bio, key_id);
      kmip_free_buffer(ctx, key_id, key_id_size);

      // Free attrs
      for (int i = 0; i < attr_count; i++) {
        if (attrs[i].value) ctx->free_func(ctx->state, attrs[i].value);
      }

      if (ret != KMIP_OK) {
        ldpp_dout(this->dpp, 5) << "KMIP activate failed: " << ret << dendl;
        return -EIO;
      }

      return 0;
    }
  };

  ldpp_dout(dpp, 20) << "kmip: rgw_kmip_manager=" << (void*)rgw_kmip_manager << dendl;
  ldpp_dout(dpp, 20) << "kmip: dpp->get_cct()=" << (void*)dpp->get_cct() << dendl;

  if (!rgw_kmip_manager) {
    ldpp_dout(dpp, 0) << "FATAL: rgw_kmip_manager NULL!" << dendl;
    return -EINVAL;
  }

  const std::string key_template = "rgw-kek-" + bucket_name;
  ldpp_dout(dpp, 20) << "key_template=" << key_template << dendl;

  CreateAndActivateKey op(dpp->get_cct(), key_template, dpp);
  ldpp_dout(dpp, 20) << "&op=" << (void*)&op << dendl;

  int ret = rgw_kmip_manager->add_request(&op);
  ldpp_dout(dpp, 20) << "add_request() RETURNS: " << ret << dendl;

  if (ret < 0) {
    ldpp_dout(dpp, 0) << "add_request FAILED: " << ret << dendl;
    return ret;
  }

  ret = op.wait(dpp, y);
  if (ret < 0) {
    ldpp_dout(dpp, 0) << "Create KEK failed" << dendl;
    return ret;
  }

  kek_id_out = op.kek_id;

  ldpp_dout(dpp, 10) << "kmip: Created KEK UUID: " << kek_id_out << dendl;
  return 0;
}


int RGWKmipSSES3::destroy_bucket_key(const DoutPrefixProvider* dpp,
                                      const std::string& kek_id,
                                      optional_yield y) {

  std::string bucket_name;

  struct DestroyKey : public RGWKMIPTransceiver {
    const std::string& kek_id;
    const DoutPrefixProvider* dpp;

    DestroyKey(CephContext* cct, const std::string& kek, const DoutPrefixProvider* dpp_in)
      : RGWKMIPTransceiver(cct, RGWKMIPTransceiver::DESTROY),
        kek_id(kek),
        dpp(dpp_in) {}

    int execute(KMIP* ctx, BIO* bio) override {
      ldpp_dout(this->dpp, 10) << "DestroyKey: Destroying KEK: " << kek_id << dendl;

      int result = kmip_bio_destroy_symmetric_key(
            bio,
            const_cast<char*>(kek_id.c_str()),
            kmip_strnlen_s(kek_id.c_str(), 50)
      );

      ldpp_dout(this->dpp, 20) << "KMIP destroy returned: " << result << dendl;

      if (result < 0) {
        ldpp_dout(this->dpp, 0) << "KMIP destroy failed: " << result << dendl;
        return -EIO;
      }

      ldpp_dout(this->dpp, 20) << "KMIP destroy succeeded (status=" << result << ")" << dendl;
      return 1;
    }
  };

  DestroyKey op(dpp->get_cct(), kek_id, dpp);

  int ret = rgw_kmip_manager->add_request(&op);
  if (ret < 0) return ret;

  ret = op.wait(dpp, y);
  if (ret < 0) {
    ldpp_dout(dpp, 0) << "Destroy KEK failed: " << cpp_strerror(ret) << dendl;
  } else {
    ldpp_dout(dpp, 10) << "Successfully destroyed KEK: " << kek_id << dendl;
  }
  return ret;
}


int RGWKmipSSES3::generate_and_wrap_dek(const DoutPrefixProvider* dpp,
                                         const std::string& kek_id,
                                         const std::string& encryption_context,
                                         bufferlist& plaintext_dek_out,
                                         bufferlist& wrapped_dek_out,
                                         optional_yield y) {

  // Generate random DEK
  unsigned char dek[32];
  if (RAND_bytes(dek, 32) != 1) {
    ldpp_dout(dpp, 0) << "Failed to generate DEK" << dendl;
    return -EIO;
  }

  plaintext_dek_out.clear();
  plaintext_dek_out.append((char*)dek, 32);
  ldpp_dout(dpp, 10) << "Wrapping DEK with KEK: " << kek_id << dendl;

  // Wrap DEK with KMIP
  struct WrapDEK : public RGWKMIPTransceiver {
    const std::string& kek_id;
    const unsigned char* dek_ptr;
    std::string encryption_context;
    bufferlist wrapped_dek;
    const DoutPrefixProvider* dpp;

    WrapDEK(CephContext* cct, const std::string& kek, const unsigned char* dek_ptr, const std::string& ctx, const DoutPrefixProvider* dpp_in)
  : RGWKMIPTransceiver(cct, RGWKMIPTransceiver::ENCRYPT),
    kek_id(kek),
    dek_ptr(dek_ptr),
    encryption_context(ctx),
    dpp(dpp_in) {
      if (!encryption_context.empty() && encryption_context.back() == '\0') {
          encryption_context.pop_back();
      }
    }

    int execute(KMIP* ctx, BIO* bio) override {
      wrapped_dek.clear();
      kmip_init(ctx, NULL, 0, KMIP_1_4);
      ldpp_dout(dpp, 20) << "KEK_ID: '" << kek_id << "' (len=" << kek_id.length() << ")" << dendl;

      CryptographicParameters params;
      memset(&params, 0, sizeof(params));
      kmip_init_cryptographic_parameters(&params);
      params.cryptographic_algorithm = KMIP_CRYPTOALG_AES;
      params.block_cipher_mode = KMIP_BLOCK_GCM;
      params.padding_method = KMIP_PAD_NONE;
      params.random_iv = KMIP_TRUE;
      params.tag_length = 16;
      const uint8_t* aad_ptr = reinterpret_cast<const uint8_t*>(encryption_context.c_str());
      int aad_len = static_cast<int>(encryption_context.length());

      ldpp_dout(dpp, 10) << "AAD: '" << encryption_context << "' (len=" << aad_len << ")" << dendl;
      ldpp_dout(dpp, 10) << "AAD (hex): " << hex_dump(reinterpret_cast<const uint8_t*>(aad_ptr), aad_len) << dendl;

      uint8_t* ciphertext = NULL;
      int ciphertext_size = 0;
      uint8_t* iv = NULL;
      int iv_size = 0;
      uint8_t* tag = NULL;
      int tag_size = 0;

      int ret = kmip_bio_encrypt_with_context(
          ctx, bio,
          const_cast<char*>(kek_id.c_str()), (int)kek_id.length(),
          const_cast<uint8_t*>(dek_ptr), 32,
          const_cast<uint8_t*>(aad_ptr), aad_len,
          &params,
          &ciphertext, &ciphertext_size,
          &iv, &iv_size,
          &tag, &tag_size
      );

      if (ret != KMIP_OK) {
        ldpp_dout(dpp, 0) << "KMIP encrypt failed: " << ret << dendl;
        memset(ciphertext, 0, ciphertext_size);
        memset(iv, 0, iv_size);
        memset(tag, 0, tag_size);
      
        if (ciphertext) ctx->free_func(ctx->state, ciphertext);
        if (iv) ctx->free_func(ctx->state, iv);
        if (tag) ctx->free_func(ctx->state, tag);
        return -EIO;
      }

      ldpp_dout(dpp, 10) << "Encrypt SUCCESS:" << dendl;
      ldpp_dout(dpp, 10) << "  IV size: " << iv_size << " bytes" << dendl;
      ldpp_dout(dpp, 10) << "  IV (hex): " << hex_dump(iv, iv_size) << dendl;
      ldpp_dout(dpp, 10) << "  Tag size: " << tag_size << " bytes" << dendl;
      ldpp_dout(dpp, 10) << "  Tag (hex): " << hex_dump(tag, tag_size) << dendl;
      ldpp_dout(dpp, 10) << "  Ciphertext size: " << ciphertext_size << " bytes" << dendl;
      ldpp_dout(dpp, 10) << "  Ciphertext (hex): " << hex_dump(ciphertext, ciphertext_size) << dendl;

      uint32_t iv_sz_n = htonl(iv_size);
      uint32_t tag_sz_n = htonl(tag_size);

     ldpp_dout(dpp, 10) << "Building wrapped buffer:" << dendl;
     ldpp_dout(dpp, 10) << "  Header: iv_size=" << iv_size << " tag_size=" << tag_size << dendl;

      wrapped_dek.append((char*)&iv_sz_n, 4);
      wrapped_dek.append((char*)&tag_sz_n, 4);
      wrapped_dek.append((char*)iv, iv_size);
      wrapped_dek.append((char*)tag, tag_size);
      wrapped_dek.append((char*)ciphertext, ciphertext_size);

      ldpp_dout(dpp, 10) << "Wrapped DEK total size: " << wrapped_dek.length() << " bytes" << dendl;
      ldpp_dout(dpp, 10) << "  Expected: 8 + " << iv_size << " + " << tag_size
                    << " + " << ciphertext_size << " = "
                    << (8 + iv_size + tag_size + ciphertext_size) << dendl;

      memset(ciphertext, 0, ciphertext_size);
      memset(iv, 0, iv_size);
      memset(tag, 0, tag_size);

      if (ciphertext) ctx->free_func(ctx->state, ciphertext);
      if (iv) ctx->free_func(ctx->state, iv);
      if (tag) ctx->free_func(ctx->state, tag);

      ldpp_dout(dpp, 10) << "Encrypt SUCCESS ct=" << ciphertext_size
                        << " iv=" << iv_size << dendl;
      return 1;
    }

  };

  WrapDEK op(cct, kek_id, dek, encryption_context, dpp);

  int ret = rgw_kmip_manager->add_request(&op);
  if (ret < 0) {
    explicit_bzero(dek, 32);  // Clean up on error
    return ret;
  }
  if (ret < 0) return ret;

  ret = op.wait(dpp, y);
  // Always zero sensitive data
  explicit_bzero(dek, 32);
  if (ret < 0) {
    ldpp_dout(dpp, 0) << "kmip debug: Wrap DEK failed" << dendl;
    return ret;
  }

  wrapped_dek_out = std::move(op.wrapped_dek);
  ldpp_dout(dpp, 10) << "kmip debug: Successfully wrapped DEK" << dendl;
  return 0;
}

int RGWKmipSSES3::unwrap_dek(const DoutPrefixProvider* dpp,
                              const std::string& kek_id,
                              const bufferlist& wrapped_dek,
                              const std::string& encryption_context,
                              bufferlist& plaintext_dek_out,
                              optional_yield y) {

  ldpp_dout(dpp, 10) << "kmip debug: Unwrapping DEK with KEK: " << kek_id << dendl;
  ldpp_dout(dpp, 10) << "KEK_ID: '" << kek_id << "' (len=" << kek_id.length() << ")" << dendl;
  ldpp_dout(dpp, 10) << "Wrapped DEK buffer size: " << wrapped_dek.length() << " bytes" << dendl;

  if (wrapped_dek.length() < 8) {
    ldpp_dout(dpp, 0) << "KMIP ERROR: Metadata size mismatch" << dendl;
    return -EINVAL;
  }

  struct UnwrapDEK : public RGWKMIPTransceiver {
    const std::string& kek_id;
    const bufferlist& wrapped_dek;
    std::string encryption_context;
    bufferlist plaintext_dek;
    const DoutPrefixProvider* dpp;

    UnwrapDEK(CephContext *cct, const std::string& kek, const bufferlist& wrapped, const std::string& context, const DoutPrefixProvider* dpp_in)
    : RGWKMIPTransceiver(cct, RGWKMIPTransceiver::DECRYPT),
      kek_id(kek),
      wrapped_dek(wrapped),
      encryption_context(context),
      dpp(dpp_in) {}

    int execute(KMIP* ctx, BIO* bio) override {
      kmip_init(ctx, NULL, 0, KMIP_1_4);

      // Header(8) + IV(12) + Tag(16) + min Ciphertext(16) = 52
      if (wrapped_dek.length() < 52) {
        ldpp_dout(dpp, 0) << "KMIP ERROR: wrapped_dek buffer is too small: " << wrapped_dek.length() << dendl;
        return -EINVAL;
      }

      std::vector<char> buffer(wrapped_dek.length());
      wrapped_dek.begin().copy(wrapped_dek.length(), buffer.data());
      const char* raw_data = buffer.data();

      ldpp_dout(dpp, 10) << "Copied to contiguous buffer: " << buffer.size() << " bytes" << dendl;
      ldpp_dout(dpp, 10) << "First 32 bytes (hex): "
                        << hex_dump(reinterpret_cast<const uint8_t*>(raw_data),
                                std::min((size_t)32, buffer.size())) << dendl;

      // 1. Safe extraction of header sizes
      uint32_t iv_size_net, tag_size_net;
      memcpy(&iv_size_net, raw_data, 4);
      memcpy(&tag_size_net, raw_data + 4, 4);
      uint32_t iv_size = ntohl(iv_size_net);
      uint32_t tag_size = ntohl(tag_size_net);

      // 2. Map Pointers using dynamic offsets
      const uint8_t* iv_ptr = reinterpret_cast<const uint8_t*>(raw_data + 8);
      const uint8_t* tag_ptr = iv_ptr + iv_size;
      const uint8_t* ct_ptr  = tag_ptr + tag_size;

      ldpp_dout(dpp, 20) << "Parsed header:" << dendl;
      ldpp_dout(dpp, 20) << "  IV size: " << iv_size << " bytes" << dendl;
      ldpp_dout(dpp, 20) << "  Tag size: " << tag_size << " bytes" << dendl;

      // 3. Calculate remaining bytes for ciphertext
      int ct_size = wrapped_dek.length() - 8 - iv_size - tag_size;

      ldpp_dout(dpp, 20) << "  Ciphertext size: " << ct_size << " bytes" << dendl;
      ldpp_dout(dpp, 20) << "  Total: 8 + " << iv_size << " + " << tag_size
                        << " + " << ct_size << " = "
                        << (8 + iv_size + tag_size + ct_size) << dendl;

      if (ct_size <= 0 || tag_size != 16) {
        ldpp_dout(dpp, 0) << "KMIP ERROR: Invalid buffer parsing. Header claims sizes that exceed buffer." << dendl;
        return -EINVAL;
      }

      ldpp_dout(dpp, 20) << "IV (hex): " << hex_dump(iv_ptr, iv_size) << dendl;
      ldpp_dout(dpp, 20) << "Tag (hex): " << hex_dump(tag_ptr, tag_size) << dendl;
      ldpp_dout(dpp, 20) << "Ciphertext (hex): " << hex_dump(ct_ptr, ct_size) << dendl;

      CryptographicParameters params;
      memset(&params, 0, sizeof(params));
      kmip_init_cryptographic_parameters(&params);
      params.cryptographic_algorithm = KMIP_CRYPTOALG_AES;
      params.block_cipher_mode = KMIP_BLOCK_GCM;
      params.padding_method = KMIP_PAD_NONE;
      params.tag_length = 16;
      const uint8_t* aad_ptr = reinterpret_cast<const uint8_t*>(encryption_context.c_str());
      int aad_len = static_cast<int>(encryption_context.length());

      ldpp_dout(dpp, 20) << "AAD: '" << encryption_context << "' (len=" << aad_len << ")" << dendl;
      ldpp_dout(dpp, 20) << "AAD (hex): " << hex_dump(aad_ptr, aad_len) << dendl;

      uint8_t* plaintext = nullptr;
      int32_t plaintext_size = 0;

      ldpp_dout(dpp, 10) << "Calling kmip_bio_decrypt_with_context..." << dendl;

      int ret = kmip_bio_decrypt_with_context(
        ctx, bio,
        const_cast<char*>(kek_id.c_str()), (int)kek_id.length(),
        const_cast<uint8_t*>(ct_ptr), ct_size,
        const_cast<uint8_t*>(aad_ptr), aad_len,
        const_cast<uint8_t*>(iv_ptr), iv_size,
        const_cast<uint8_t*>(tag_ptr), tag_size, // Passing the Tag
        &params,
        &plaintext, &plaintext_size
      );

      ldpp_dout(dpp, 20) << "Decrypt returned: " << ret << " (KMIP_OK=" << KMIP_OK << ")" << dendl;
      ldpp_dout(dpp, 20) << "Plaintext size: " << plaintext_size << " bytes" << dendl;

      if (ret != KMIP_OK || plaintext_size != 32) {
        ldpp_dout(dpp, 20) << "KMIP Decrypt failed on server (InvalidTag). Return code: " << ret << dendl;
        if (plaintext) {
          ldpp_dout(dpp, 20) << "Partial plaintext: " << hex_dump(plaintext, plaintext_size) << dendl;
          kmip_free_buffer(ctx, plaintext, plaintext_size);
        }
        return -EIO;
      }

      ldpp_dout(dpp, 20) << "Decrypted DEK (hex): " << hex_dump(plaintext, 32) << dendl;

      if (ret == KMIP_OK && plaintext_size == 32) {
        plaintext_dek.append(reinterpret_cast<char*>(plaintext), 32);
        ::ceph::crypto::zeroize_for_security(plaintext, plaintext_size);
        kmip_free_buffer(ctx, plaintext, plaintext_size);
        ldpp_dout(dpp, 20) << "EXECUTE: plaintext_dek.length()=" << plaintext_dek.length() << dendl;
        ldpp_dout(dpp, 20) << "EXECUTE: plaintext_dek (hex)="
                         << hex_dump((uint8_t*)plaintext_dek.c_str(), plaintext_dek.length()) << dendl;
        ldpp_dout(dpp, 20) << "EXECUTE: About to return 1" << dendl;
        return 1;
      }
      return 0;
    }
  };

  UnwrapDEK op(cct, kek_id, wrapped_dek, encryption_context, dpp);

  int ret = this->rgw_kmip_manager->add_request(&op);
  if (ret < 0) return ret;

  ret = op.wait(dpp, y);
  ldpp_dout(dpp, 20) << "AFTER wait(): ret=" << ret << dendl;
  ldpp_dout(dpp, 20) << "AFTER wait(): op.plaintext_dek.length()=" << op.plaintext_dek.length() << dendl;
  ldpp_dout(dpp, 20) << "AFTER wait(): op.plaintext_dek (hex)="
                  << hex_dump((uint8_t*)op.plaintext_dek.c_str(), op.plaintext_dek.length()) << dendl;
  if (ret < 0) {
    ldpp_dout(dpp, 0) << "Unwrap DEK failed" << dendl;
    return ret;
  }

  plaintext_dek_out =  std::move(op.plaintext_dek);

  ldpp_dout(dpp, 20) << "AFTER unwrap: plaintext_dek_out.length()=" << plaintext_dek_out.length() << dendl;
  ldpp_dout(dpp, 20) << "AFTER unwrap: plaintext_dek_out (hex)="
                    << hex_dump((uint8_t*)plaintext_dek_out.c_str(), plaintext_dek_out.length()) << dendl;
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
