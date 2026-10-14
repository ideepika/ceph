#!/usr/bin/env bash
#
# Correctness scenarios and an A/B for osd_backfill_digest.
#
#   backfill_digest.sh correctness   # scenarios on a fresh vstart cluster
#   backfill_digest.sh ab            # timing A/B on one filled cluster
#
# CLUSTER=vstart (default) starts its own cluster from $BUILD and only ever
# kills daemons started from that build directory. CLUSTER=external uses
# the cluster in $CEPH_CONF and the OSD_STOP / OSD_START / OSD_KILL
# templates, where {} is the OSD id, e.g.
#   OSD_STOP='ssh node{h} systemctl stop ceph-osd@{}'
# (OSD_HOST_<id>=nodeX is substituted for {h}).

set -euo pipefail

MODE=${1:?usage: $0 correctness|ab}
CLUSTER=${CLUSTER:-vstart}
BUILD=${BUILD:-$PWD}
OUT=${OUT:-$HOME/results/backfill-digest/$(date +%Y-%m-%d-%H%M)-$MODE}
POOL=${POOL:-bfdigest}
PGS=${PGS:-16}
NOSD=${NOSD:-5}
SIZE=${SIZE:-3}
NOBJ=${NOBJ:-4000}
OBJSIZE=${OBJSIZE:-4096}
CHANGE=${CHANGE:-200}
DELETE=${DELETE:-50}
PAR=${PAR:-32}
REPS=${REPS:-3}
DOWN=${DOWN:-1}
VERIFY=${VERIFY:-false}
EXTRA_OPTS=${EXTRA_OPTS:-}

mkdir -p "$OUT"
LOG=$OUT/run.log
exec > >(tee -a "$LOG") 2>&1

log() { echo "$(date +%T) $*"; }
die() { log "FAIL: $*"; exit 1; }

if [ "$CLUSTER" = vstart ]; then
  cd "$BUILD"
  export PATH=$BUILD/bin:$PATH
  export CEPH_CONF=$BUILD/ceph.conf
fi
CEPH="ceph --connect-timeout 30"
export CEPH_ARGS="--log-to-stderr=false ${CEPH_ARGS:-}"

# ---------------------------------------------------------------- cluster --

vstart_kill_all() {
  pkill -9 -f "[/]${BUILD#/}/bin/ceph-" 2>/dev/null || true
  for _ in $(seq 30); do
    pgrep -f "[/]${BUILD#/}/bin/ceph-" >/dev/null || return 0
    sleep 1
  done
}

start_cluster() {
  [ "$CLUSTER" = vstart ] || return 0
  vstart_kill_all
  rm -rf dev out
  local opts=(
    -o "osd_min_pg_log_entries = 1"
    -o "osd_max_pg_log_entries = 2"
    -o "osd_pg_log_trim_min = 0"
    -o "osd_pool_default_pg_autoscale_mode = off"
    -o "osd_op_queue = wpq"
    -o "osd_backfill_digest = ${DIGEST:-true}"
    -o "osd_backfill_digest_verify = $VERIFY"
    -o "debug_osd = ${DEBUG_OSD:-10}"
  )
  local o
  for o in $EXTRA_OPTS; do
    opts+=(-o "${o/=/ = }")
  done
  MON=1 OSD=$NOSD MDS=0 MGR=1 RGW=0 NFS=0 ../src/vstart.sh -n -d \
    --without-dashboard "${opts[@]}" > "$OUT/vstart.log" 2>&1 ||
    die "vstart, see $OUT/vstart.log"
  log "vstart up: $NOSD osds"
}

stop_cluster() {
  [ "$CLUSTER" = vstart ] && vstart_kill_all
  return 0
}

osd_cmd() {   # template id
  local t=${1//\{\}/$2} hv="OSD_HOST_$2"
  t=${t//\{h\}/${!hv:-}}
  eval "$t"
}

osd_stop() {   # id [TERM|KILL]
  local sig=${2:-TERM}
  if [ "$CLUSTER" = vstart ]; then
    local pid; pid=$(cat "out/osd.$1.pid")
    kill -"$sig" "$pid"
    for _ in $(seq 120); do kill -0 "$pid" 2>/dev/null || break; sleep 0.5; done
  elif [ "$sig" = KILL ]; then
    osd_cmd "${OSD_KILL:?}" "$1"
  else
    osd_cmd "${OSD_STOP:?}" "$1"
  fi
  $CEPH osd down "$1" >/dev/null 2>&1 || true
  for _ in $(seq 60); do
    $CEPH osd dump -f json | python3 -c "
import json,sys; d=json.load(sys.stdin)
sys.exit(0 if [o for o in d['osds'] if o['osd']==$1 and not o['up']] else 1)" && return 0
    sleep 1
  done
  die "osd.$1 not down"
}

osd_start() {
  if [ "$CLUSTER" = vstart ]; then
    bin/ceph-osd -i "$1" -c "$CEPH_CONF" >> "$OUT/osd.$1.restart.log" 2>&1
  else
    osd_cmd "${OSD_START:?}" "$1"
  fi
}

wait_clean() {   # [timeout s]
  local t=${1:-1800} s
  for _ in $(seq "$t"); do
    s=$($CEPH pg stat 2>/dev/null || true)
    if [[ $s == *active+clean* && $s != *backfill* && $s != *recover* &&
          $s != *peering* && $s != *degraded* && $s != *undersized* &&
          $s != *remapped* && $s != *stale* && $s != *activating* ]] &&
       [[ $s =~ ([0-9]+)\ pgs:\ ([0-9]+)\ active\+clean ]] &&
       [ "${BASH_REMATCH[1]}" = "${BASH_REMATCH[2]}" ]; then
      return 0
    fi
    sleep 1
  done
  return 1
}

counters() {   # -> json {osd: {counter: value}}
  local ids; ids=$($CEPH osd ls)
  for i in $ids; do
    echo "$i $($CEPH tell osd.$i perf dump osd 2>/dev/null | tr -d '\n' || echo '{}')"
  done | python3 -c '
import json,sys
out={}
for line in sys.stdin:
    i,_,j=line.partition(" ")
    try: d=json.loads(j).get("osd",{})
    except Exception: d={}
    out[i]={k:v for k,v in d.items() if k.startswith("backfill_")}
print(json.dumps(out))'
}

counter_delta() {   # before after -> "name=total ..." summed over osds
  python3 - "$1" "$2" <<'EOF'
import json,sys
a,b=json.loads(sys.argv[1]),json.loads(sys.argv[2])
tot={}
for o,c in b.items():
    for k,v in c.items():
        tot[k]=tot.get(k,0)+v-a.get(o,{}).get(k,0)
print(" ".join(f"{k.replace('backfill_','')}={v}" for k,v in sorted(tot.items())))
EOF
}

# --------------------------------------------------------------- objects --
# expected content lives in $OUT/expect: "name payload"

mkpayloads() {
  for p in a b c d; do
    head -c "$OBJSIZE" /dev/urandom > "$OUT/pay.$p"
  done
}

put_many() {   # payload < names
  xargs -P "$PAR" -I{} rados -p "$POOL" put {} "$OUT/pay.$1" >/dev/null
}

expect_set() {   # payload < names
  local tmp; tmp=$(mktemp)
  cat > "$tmp"
  python3 - "$OUT/expect" "$1" "$tmp" <<'EOF'
import sys
f,p,names=sys.argv[1],sys.argv[2],sys.argv[3]
d={}
try:
    for l in open(f): n,v=l.split(); d[n]=v
except FileNotFoundError: pass
for n in open(names).read().split():
    if p=="-": d.pop(n,None)
    else: d[n]=p
with open(f,"w") as o:
    for n,v in sorted(d.items()): o.write(f"{n} {v}\n")
EOF
  rm -f "$tmp"
}

write_objs() {   # payload < names
  local names; names=$(cat)
  echo "$names" | put_many "$1"
  echo "$names" | expect_set "$1"
}

delete_objs() {   # < names
  local names; names=$(cat)
  echo "$names" | xargs -P "$PAR" -I{} rados -p "$POOL" rm {} 2>/dev/null || true
  echo "$names" | expect_set -
}

pick() {   # n -> n random existing names
  cut -d' ' -f1 "$OUT/expect" | shuf -n "$1"
}

verify_content() {
  local tag=$1 bad=0
  rados -p "$POOL" ls | sort > "$OUT/$tag.ls"
  cut -d' ' -f1 "$OUT/expect" > "$OUT/$tag.want"
  # rados ls also lists deleted heads that still have clones; only a head
  # that stats is really there
  local missing extra
  missing=$(comm -23 "$OUT/$tag.want" "$OUT/$tag.ls" | wc -l)
  extra=$(comm -13 "$OUT/$tag.want" "$OUT/$tag.ls" |
    xargs -r -P "$PAR" -I{} sh -c 'rados -p '"$POOL"' stat "{}" >/dev/null 2>&1 && echo "{}"' | wc -l)
  if [ "$missing" != 0 ] || [ "$extra" != 0 ]; then
    log "$tag: object set differs: $missing missing, $extra extra"
    bad=1
  fi
  for p in a b c d; do
    md5sum < "$OUT/pay.$p" | cut -d' ' -f1 > "$OUT/pay.$p.md5"
  done
  local mism
  mism=$(awk '{print $1, $2}' "$OUT/expect" | xargs -P "$PAR" -n2 sh -c '
    h=$(rados -p '"$POOL"' get "$1" - 2>/dev/null | md5sum | cut -d" " -f1)
    [ "$h" = "$(cat '"$OUT"'/pay.$2.md5)" ] || echo "$1"' _ | wc -l)
  if [ "$mism" != 0 ]; then
    log "$tag: $mism objects with wrong content"
    bad=1
  fi
  deep_scrub "$tag" || bad=1
  return $bad
}

deep_scrub() {
  local tag=$1 pgs since
  since=$(date +%Y-%m-%dT%H:%M:%S)
  pgs=$($CEPH pg ls-by-pool "$POOL" -f json | python3 -c '
import json,sys; d=json.load(sys.stdin)
d=d.get("pg_stats") or [] if isinstance(d,dict) else d
print(" ".join(p["pgid"] for p in d))')
  for pg in $pgs; do $CEPH pg deep-scrub "$pg" >/dev/null 2>&1; done
  for _ in $(seq 600); do
    local left
    left=$($CEPH pg ls-by-pool "$POOL" -f json | python3 -c "
import json,sys; d=json.load(sys.stdin)
d=d.get('pg_stats') or [] if isinstance(d,dict) else d
print(sum(1 for p in d if p['last_deep_scrub_stamp'][:19] < '$since'))")
    [ "$left" = 0 ] && break
    sleep 2
  done
  local inc
  inc=$($CEPH pg ls-by-pool "$POOL" inconsistent -f json | python3 -c '
import json,sys; d=json.load(sys.stdin)
d=d.get("pg_stats") or [] if isinstance(d,dict) else d; print(len(d))')
  if [ "$inc" != 0 ]; then
    log "$tag: $inc inconsistent pgs after deep-scrub"
    return 1
  fi
}

make_pool() {
  $CEPH osd pool create "$POOL" "$PGS" "$PGS" replicated >/dev/null
  $CEPH osd pool set "$POOL" size "$SIZE" >/dev/null
  $CEPH osd pool set "$POOL" min_size 1 --yes-i-really-mean-it >/dev/null 2>&1 ||
    $CEPH osd pool set "$POOL" min_size 1 >/dev/null
  $CEPH osd pool set "$POOL" pg_autoscale_mode off >/dev/null
  $CEPH osd pool application enable "$POOL" rados >/dev/null
  wait_clean 300 || die "pool not clean"
}

fill() {
  log "fill: $NOBJ x $OBJSIZE B"
  : > "$OUT/expect"
  seq -f 'obj%07g' 1 "$NOBJ" | write_objs a
  wait_clean 600 || die "not clean after fill"
}

# one backfill round: stop osds, run a mutation, start, wait clean
round() {   # tag sig "osds" mutate_fn [during_fn]
  local tag=$1 sig=$2 osds=$3 mutate=$4 during=${5:-}
  $CEPH osd set noout >/dev/null 2>&1
  for o in $osds; do osd_stop "$o" "$sig"; done
  $mutate
  local c0 t0 t1
  c0=$(counters)
  t0=$(date +%s.%N)
  for o in $osds; do osd_start "$o"; done
  [ -n "$during" ] && $during
  wait_clean || die "$tag: never clean"
  t1=$(date +%s.%N)
  $CEPH osd unset noout >/dev/null 2>&1
  ROUND_S=$(awk -v a="$t1" -v b="$t0" 'BEGIN{printf "%.2f", a-b}')
  ROUND_C=$(counter_delta "$c0" "$(counters)")
  log "$tag: clean after ${ROUND_S}s  $ROUND_C"
}

# ------------------------------------------------------------- scenarios --

m_overwrite() {
  pick "$CHANGE" | write_objs b
  seq -f "new$RANDOM-%05g" 1 $((CHANGE / 2)) | write_objs c
  pick "$DELETE" | delete_objs
}

during_churn() {
  # writes and deletes racing the backfill, slowed down by recovery sleep
  ( for _ in $(seq 20); do
      pick 10 | write_objs d
      pick 5 | delete_objs
      seq -f "race$RANDOM-%03g" 1 5 | write_objs a
      sleep 0.5
    done ) &
  CHURN_PID=$!
}

m_snap() {
  rados -p "$POOL" mksnap "s$RANDOM" >/dev/null
  pick "$CHANGE" | write_objs c
  pick "$DELETE" | delete_objs
}

m_rmsnap() {
  local s
  for s in $(rados -p "$POOL" lssnap | awk '$2 ~ /^s[0-9]+$/ {print $2}'); do
    rados -p "$POOL" rmsnap "$s" >/dev/null
  done
  pick "$CHANGE" | write_objs b
}

check() {   # tag
  if verify_content "$1"; then
    log "$1: content OK"
  else
    FAILED+=("$1")
  fi
}

correctness() {
  FAILED=()
  start_cluster
  make_pool
  mkpayloads
  fill
  check baseline

  local other=$(( (DOWN + 1) % NOSD ))
  round clean-stop TERM "$DOWN" m_overwrite;           check clean-stop
  round kill-9 KILL "$DOWN" m_overwrite;               check kill-9
  round two-targets TERM "$DOWN $other" m_overwrite;   check two-targets
  $CEPH config set osd osd_recovery_sleep 0.2 >/dev/null
  $CEPH config set osd osd_recovery_sleep_degraded 0.2 >/dev/null
  round churn TERM "$DOWN" m_overwrite during_churn
  wait "$CHURN_PID" || true
  wait_clean || die "churn: never clean"
  $CEPH config rm osd osd_recovery_sleep >/dev/null
  $CEPH config rm osd osd_recovery_sleep_degraded >/dev/null
  check churn
  round snapshots TERM "$DOWN" m_snap;                 check snapshots
  round snap-trim TERM "$DOWN" m_rmsnap;               check snap-trim

  log "counters: $(counter_delta '{}' "$(counters)")"
  stop_cluster
  if [ ${#FAILED[@]} -gt 0 ]; then
    die "scenarios failed: ${FAILED[*]}"
  fi
  log "PASS: all scenarios"
}

# -------------------------------------------------------------------- ab --

m_ab() {
  pick "$CHANGE" | write_objs b
  [ "$DELETE" -gt 0 ] && pick "$DELETE" | delete_objs
  return 0
}
m_none() { :; }

ab() {
  start_cluster
  make_pool
  mkpayloads
  fill
  log "ab: $NOBJ objects, $PGS pgs, $CHANGE changed + $DELETE deleted per round"
  echo "rep arm floor_s total_s backfill_s counters" > "$OUT/ab.tsv"
  for rep in $(seq "$REPS"); do
    local arms="false true"
    [ $((rep % 2)) = 0 ] && arms="true false"
    for arm in $arms; do
      $CEPH config set osd osd_backfill_digest "$arm" >/dev/null
      round "rep$rep-$arm-floor" TERM "$DOWN" m_none
      local floor=$ROUND_S
      round "rep$rep-$arm" TERM "$DOWN" m_ab
      local bf; bf=$(awk -v a="$ROUND_S" -v b="$floor" 'BEGIN{printf "%.2f", a-b}')
      echo "$rep $arm $floor $ROUND_S $bf $ROUND_C" >> "$OUT/ab.tsv"
      log "rep$rep digest=$arm: floor ${floor}s total ${ROUND_S}s backfill ${bf}s"
    done
  done
  [ "${SKIP_VERIFY:-0}" = 1 ] || check ab-final || true
  stop_cluster
  column -t "$OUT/ab.tsv" | cut -c1-200
}

trap 'stop_cluster' EXIT
case $MODE in
  correctness) correctness ;;
  ab) ab ;;
  *) die "unknown mode $MODE" ;;
esac
log "results in $OUT"
