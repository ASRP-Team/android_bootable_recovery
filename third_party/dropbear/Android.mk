# Dropbear SSH server for AERA recovery.
#
# Server only build. Password authentication is delegated to AERA
# (src/aera_auth.c) because Android's libc has no crypt(3); public key
# authentication uses the regular Dropbear code path.
#
# Configuration headers (config.h, localoptions.h, default_options_guard.h)
# live in android/ and are picked up with -I.

LOCAL_PATH := $(call my-dir)

DROPBEAR_CFLAGS := \
    -DLOCALOPTIONS_H_EXISTS \
    -D_FORTIFY_SOURCE=2 \
    -fstack-protector-strong \
    -Wno-pointer-sign \
    -Os -W -Wall

DROPBEAR_INCLUDES := \
    $(LOCAL_PATH)/android \
    $(LOCAL_PATH)/src \
    $(LOCAL_PATH)/libtomcrypt/src/headers \
    $(LOCAL_PATH)/libtommath

# ---------------------------------------------------------------------------
# libtommath
# ---------------------------------------------------------------------------
include $(CLEAR_VARS)
LOCAL_MODULE := libaera_tommath
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(DROPBEAR_INCLUDES)
LOCAL_CFLAGS := $(DROPBEAR_CFLAGS)
LOCAL_SRC_FILES := \
    libtommath/bn_cutoffs.c libtommath/bn_deprecated.c libtommath/bn_mp_2expt.c libtommath/bn_mp_abs.c \
    libtommath/bn_mp_add.c libtommath/bn_mp_add_d.c libtommath/bn_mp_addmod.c libtommath/bn_mp_and.c \
    libtommath/bn_mp_clamp.c libtommath/bn_mp_clear.c libtommath/bn_mp_clear_multi.c libtommath/bn_mp_cmp.c \
    libtommath/bn_mp_cmp_d.c libtommath/bn_mp_cmp_mag.c libtommath/bn_mp_cnt_lsb.c libtommath/bn_mp_complement.c \
    libtommath/bn_mp_copy.c libtommath/bn_mp_count_bits.c libtommath/bn_mp_decr.c libtommath/bn_mp_div.c \
    libtommath/bn_mp_div_2.c libtommath/bn_mp_div_2d.c libtommath/bn_mp_div_3.c libtommath/bn_mp_div_d.c \
    libtommath/bn_mp_dr_is_modulus.c libtommath/bn_mp_dr_reduce.c libtommath/bn_mp_dr_setup.c libtommath/bn_mp_error_to_string.c \
    libtommath/bn_mp_exch.c libtommath/bn_mp_expt_u32.c libtommath/bn_mp_exptmod.c libtommath/bn_mp_exteuclid.c \
    libtommath/bn_mp_fread.c libtommath/bn_mp_from_sbin.c libtommath/bn_mp_from_ubin.c libtommath/bn_mp_fwrite.c \
    libtommath/bn_mp_gcd.c libtommath/bn_mp_get_i32.c libtommath/bn_mp_get_i64.c libtommath/bn_mp_get_l.c \
    libtommath/bn_mp_get_ll.c libtommath/bn_mp_get_mag_u32.c libtommath/bn_mp_get_mag_u64.c libtommath/bn_mp_get_mag_ul.c \
    libtommath/bn_mp_get_mag_ull.c libtommath/bn_mp_grow.c libtommath/bn_mp_incr.c libtommath/bn_mp_init.c \
    libtommath/bn_mp_init_copy.c libtommath/bn_mp_init_i32.c libtommath/bn_mp_init_i64.c libtommath/bn_mp_init_l.c \
    libtommath/bn_mp_init_ll.c libtommath/bn_mp_init_multi.c libtommath/bn_mp_init_set.c libtommath/bn_mp_init_size.c \
    libtommath/bn_mp_init_u32.c libtommath/bn_mp_init_u64.c libtommath/bn_mp_init_ul.c libtommath/bn_mp_init_ull.c \
    libtommath/bn_mp_invmod.c libtommath/bn_mp_is_square.c libtommath/bn_mp_iseven.c libtommath/bn_mp_isodd.c \
    libtommath/bn_mp_kronecker.c libtommath/bn_mp_lcm.c libtommath/bn_mp_log_u32.c libtommath/bn_mp_lshd.c \
    libtommath/bn_mp_mod.c libtommath/bn_mp_mod_2d.c libtommath/bn_mp_mod_d.c libtommath/bn_mp_montgomery_calc_normalization.c \
    libtommath/bn_mp_montgomery_reduce.c libtommath/bn_mp_montgomery_setup.c libtommath/bn_mp_mul.c libtommath/bn_mp_mul_2.c \
    libtommath/bn_mp_mul_2d.c libtommath/bn_mp_mul_d.c libtommath/bn_mp_mulmod.c libtommath/bn_mp_neg.c \
    libtommath/bn_mp_or.c libtommath/bn_mp_pack.c libtommath/bn_mp_pack_count.c libtommath/bn_mp_prime_fermat.c \
    libtommath/bn_mp_prime_frobenius_underwood.c libtommath/bn_mp_prime_is_prime.c libtommath/bn_mp_prime_miller_rabin.c libtommath/bn_mp_prime_next_prime.c \
    libtommath/bn_mp_prime_rabin_miller_trials.c libtommath/bn_mp_prime_rand.c libtommath/bn_mp_prime_strong_lucas_selfridge.c libtommath/bn_mp_radix_size.c \
    libtommath/bn_mp_radix_smap.c libtommath/bn_mp_rand.c libtommath/bn_mp_read_radix.c libtommath/bn_mp_reduce.c \
    libtommath/bn_mp_reduce_2k.c libtommath/bn_mp_reduce_2k_l.c libtommath/bn_mp_reduce_2k_setup.c libtommath/bn_mp_reduce_2k_setup_l.c \
    libtommath/bn_mp_reduce_is_2k.c libtommath/bn_mp_reduce_is_2k_l.c libtommath/bn_mp_reduce_setup.c libtommath/bn_mp_root_u32.c \
    libtommath/bn_mp_rshd.c libtommath/bn_mp_sbin_size.c libtommath/bn_mp_set.c libtommath/bn_mp_set_i32.c \
    libtommath/bn_mp_set_i64.c libtommath/bn_mp_set_l.c libtommath/bn_mp_set_ll.c libtommath/bn_mp_set_u32.c \
    libtommath/bn_mp_set_u64.c libtommath/bn_mp_set_ul.c libtommath/bn_mp_set_ull.c libtommath/bn_mp_shrink.c \
    libtommath/bn_mp_signed_rsh.c libtommath/bn_mp_sqr.c libtommath/bn_mp_sqrmod.c libtommath/bn_mp_sqrt.c \
    libtommath/bn_mp_sqrtmod_prime.c libtommath/bn_mp_sub.c libtommath/bn_mp_sub_d.c libtommath/bn_mp_submod.c \
    libtommath/bn_mp_to_radix.c libtommath/bn_mp_to_sbin.c libtommath/bn_mp_to_ubin.c libtommath/bn_mp_ubin_size.c \
    libtommath/bn_mp_unpack.c libtommath/bn_mp_xor.c libtommath/bn_mp_zero.c libtommath/bn_prime_tab.c \
    libtommath/bn_s_mp_add.c libtommath/bn_s_mp_balance_mul.c libtommath/bn_s_mp_exptmod.c libtommath/bn_s_mp_exptmod_fast.c \
    libtommath/bn_s_mp_get_bit.c libtommath/bn_s_mp_invmod_fast.c libtommath/bn_s_mp_invmod_slow.c libtommath/bn_s_mp_karatsuba_mul.c \
    libtommath/bn_s_mp_karatsuba_sqr.c libtommath/bn_s_mp_montgomery_reduce_fast.c libtommath/bn_s_mp_mul_digs.c libtommath/bn_s_mp_mul_digs_fast.c \
    libtommath/bn_s_mp_mul_high_digs.c libtommath/bn_s_mp_mul_high_digs_fast.c libtommath/bn_s_mp_prime_is_divisible.c libtommath/bn_s_mp_rand_jenkins.c \
    libtommath/bn_s_mp_rand_platform.c libtommath/bn_s_mp_reverse.c libtommath/bn_s_mp_sqr.c libtommath/bn_s_mp_sqr_fast.c \
    libtommath/bn_s_mp_sub.c libtommath/bn_s_mp_toom_mul.c libtommath/bn_s_mp_toom_sqr.c
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------------------------------------------
# libtomcrypt
# ---------------------------------------------------------------------------
include $(CLEAR_VARS)
LOCAL_MODULE := libaera_tomcrypt
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(DROPBEAR_INCLUDES)
LOCAL_CFLAGS := $(DROPBEAR_CFLAGS) -DLTC_SOURCE
LOCAL_SRC_FILES := \
    libtomcrypt/src/ciphers/aes/aes.c libtomcrypt/src/ciphers/anubis.c libtomcrypt/src/ciphers/blowfish.c libtomcrypt/src/ciphers/camellia.c \
    libtomcrypt/src/ciphers/cast5.c libtomcrypt/src/ciphers/des.c libtomcrypt/src/ciphers/kasumi.c libtomcrypt/src/ciphers/khazad.c \
    libtomcrypt/src/ciphers/kseed.c libtomcrypt/src/ciphers/multi2.c libtomcrypt/src/ciphers/noekeon.c libtomcrypt/src/ciphers/rc2.c \
    libtomcrypt/src/ciphers/rc5.c libtomcrypt/src/ciphers/rc6.c libtomcrypt/src/ciphers/safer/safer.c libtomcrypt/src/ciphers/safer/saferp.c \
    libtomcrypt/src/ciphers/skipjack.c libtomcrypt/src/ciphers/twofish/twofish.c libtomcrypt/src/ciphers/xtea.c libtomcrypt/src/encauth/ccm/ccm_add_aad.c \
    libtomcrypt/src/encauth/ccm/ccm_add_nonce.c libtomcrypt/src/encauth/ccm/ccm_done.c libtomcrypt/src/encauth/ccm/ccm_init.c libtomcrypt/src/encauth/ccm/ccm_memory.c \
    libtomcrypt/src/encauth/ccm/ccm_process.c libtomcrypt/src/encauth/ccm/ccm_reset.c libtomcrypt/src/encauth/ccm/ccm_test.c libtomcrypt/src/encauth/chachapoly/chacha20poly1305_add_aad.c \
    libtomcrypt/src/encauth/chachapoly/chacha20poly1305_decrypt.c libtomcrypt/src/encauth/chachapoly/chacha20poly1305_done.c libtomcrypt/src/encauth/chachapoly/chacha20poly1305_encrypt.c libtomcrypt/src/encauth/chachapoly/chacha20poly1305_init.c \
    libtomcrypt/src/encauth/chachapoly/chacha20poly1305_memory.c libtomcrypt/src/encauth/chachapoly/chacha20poly1305_setiv.c libtomcrypt/src/encauth/chachapoly/chacha20poly1305_setiv_rfc7905.c libtomcrypt/src/encauth/chachapoly/chacha20poly1305_test.c \
    libtomcrypt/src/encauth/eax/eax_addheader.c libtomcrypt/src/encauth/eax/eax_decrypt.c libtomcrypt/src/encauth/eax/eax_decrypt_verify_memory.c libtomcrypt/src/encauth/eax/eax_done.c \
    libtomcrypt/src/encauth/eax/eax_encrypt.c libtomcrypt/src/encauth/eax/eax_encrypt_authenticate_memory.c libtomcrypt/src/encauth/eax/eax_init.c libtomcrypt/src/encauth/eax/eax_test.c \
    libtomcrypt/src/encauth/gcm/gcm_add_aad.c libtomcrypt/src/encauth/gcm/gcm_add_iv.c libtomcrypt/src/encauth/gcm/gcm_done.c libtomcrypt/src/encauth/gcm/gcm_gf_mult.c \
    libtomcrypt/src/encauth/gcm/gcm_init.c libtomcrypt/src/encauth/gcm/gcm_memory.c libtomcrypt/src/encauth/gcm/gcm_mult_h.c libtomcrypt/src/encauth/gcm/gcm_process.c \
    libtomcrypt/src/encauth/gcm/gcm_reset.c libtomcrypt/src/encauth/gcm/gcm_test.c libtomcrypt/src/encauth/ocb/ocb_decrypt.c libtomcrypt/src/encauth/ocb/ocb_decrypt_verify_memory.c \
    libtomcrypt/src/encauth/ocb/ocb_done_decrypt.c libtomcrypt/src/encauth/ocb/ocb_done_encrypt.c libtomcrypt/src/encauth/ocb/ocb_encrypt.c libtomcrypt/src/encauth/ocb/ocb_encrypt_authenticate_memory.c \
    libtomcrypt/src/encauth/ocb/ocb_init.c libtomcrypt/src/encauth/ocb/ocb_ntz.c libtomcrypt/src/encauth/ocb/ocb_shift_xor.c libtomcrypt/src/encauth/ocb/ocb_test.c \
    libtomcrypt/src/encauth/ocb/s_ocb_done.c libtomcrypt/src/encauth/ocb3/ocb3_add_aad.c libtomcrypt/src/encauth/ocb3/ocb3_decrypt.c libtomcrypt/src/encauth/ocb3/ocb3_decrypt_last.c \
    libtomcrypt/src/encauth/ocb3/ocb3_decrypt_verify_memory.c libtomcrypt/src/encauth/ocb3/ocb3_done.c libtomcrypt/src/encauth/ocb3/ocb3_encrypt.c libtomcrypt/src/encauth/ocb3/ocb3_encrypt_authenticate_memory.c \
    libtomcrypt/src/encauth/ocb3/ocb3_encrypt_last.c libtomcrypt/src/encauth/ocb3/ocb3_init.c libtomcrypt/src/encauth/ocb3/ocb3_int_ntz.c libtomcrypt/src/encauth/ocb3/ocb3_int_xor_blocks.c \
    libtomcrypt/src/encauth/ocb3/ocb3_test.c libtomcrypt/src/hashes/blake2b.c libtomcrypt/src/hashes/blake2s.c libtomcrypt/src/hashes/chc/chc.c \
    libtomcrypt/src/hashes/helper/hash_file.c libtomcrypt/src/hashes/helper/hash_filehandle.c libtomcrypt/src/hashes/helper/hash_memory.c libtomcrypt/src/hashes/helper/hash_memory_multi.c \
    libtomcrypt/src/hashes/md2.c libtomcrypt/src/hashes/md4.c libtomcrypt/src/hashes/md5.c libtomcrypt/src/hashes/rmd128.c \
    libtomcrypt/src/hashes/rmd160.c libtomcrypt/src/hashes/rmd256.c libtomcrypt/src/hashes/rmd320.c libtomcrypt/src/hashes/sha1.c \
    libtomcrypt/src/hashes/sha2/sha224.c libtomcrypt/src/hashes/sha2/sha256.c libtomcrypt/src/hashes/sha2/sha384.c libtomcrypt/src/hashes/sha2/sha512.c \
    libtomcrypt/src/hashes/sha2/sha512_224.c libtomcrypt/src/hashes/sha2/sha512_256.c libtomcrypt/src/hashes/sha3.c libtomcrypt/src/hashes/sha3_test.c \
    libtomcrypt/src/hashes/tiger.c libtomcrypt/src/hashes/whirl/whirl.c libtomcrypt/src/mac/blake2/blake2bmac.c libtomcrypt/src/mac/blake2/blake2bmac_file.c \
    libtomcrypt/src/mac/blake2/blake2bmac_memory.c libtomcrypt/src/mac/blake2/blake2bmac_memory_multi.c libtomcrypt/src/mac/blake2/blake2bmac_test.c libtomcrypt/src/mac/blake2/blake2smac.c \
    libtomcrypt/src/mac/blake2/blake2smac_file.c libtomcrypt/src/mac/blake2/blake2smac_memory.c libtomcrypt/src/mac/blake2/blake2smac_memory_multi.c libtomcrypt/src/mac/blake2/blake2smac_test.c \
    libtomcrypt/src/mac/f9/f9_done.c libtomcrypt/src/mac/f9/f9_file.c libtomcrypt/src/mac/f9/f9_init.c libtomcrypt/src/mac/f9/f9_memory.c \
    libtomcrypt/src/mac/f9/f9_memory_multi.c libtomcrypt/src/mac/f9/f9_process.c libtomcrypt/src/mac/f9/f9_test.c libtomcrypt/src/mac/hmac/hmac_done.c \
    libtomcrypt/src/mac/hmac/hmac_file.c libtomcrypt/src/mac/hmac/hmac_init.c libtomcrypt/src/mac/hmac/hmac_memory.c libtomcrypt/src/mac/hmac/hmac_memory_multi.c \
    libtomcrypt/src/mac/hmac/hmac_process.c libtomcrypt/src/mac/hmac/hmac_test.c libtomcrypt/src/mac/omac/omac_done.c libtomcrypt/src/mac/omac/omac_file.c \
    libtomcrypt/src/mac/omac/omac_init.c libtomcrypt/src/mac/omac/omac_memory.c libtomcrypt/src/mac/omac/omac_memory_multi.c libtomcrypt/src/mac/omac/omac_process.c \
    libtomcrypt/src/mac/omac/omac_test.c libtomcrypt/src/mac/pelican/pelican.c libtomcrypt/src/mac/pelican/pelican_memory.c libtomcrypt/src/mac/pelican/pelican_test.c \
    libtomcrypt/src/mac/pmac/pmac_done.c libtomcrypt/src/mac/pmac/pmac_file.c libtomcrypt/src/mac/pmac/pmac_init.c libtomcrypt/src/mac/pmac/pmac_memory.c \
    libtomcrypt/src/mac/pmac/pmac_memory_multi.c libtomcrypt/src/mac/pmac/pmac_ntz.c libtomcrypt/src/mac/pmac/pmac_process.c libtomcrypt/src/mac/pmac/pmac_shift_xor.c \
    libtomcrypt/src/mac/pmac/pmac_test.c libtomcrypt/src/mac/poly1305/poly1305.c libtomcrypt/src/mac/poly1305/poly1305_file.c libtomcrypt/src/mac/poly1305/poly1305_memory.c \
    libtomcrypt/src/mac/poly1305/poly1305_memory_multi.c libtomcrypt/src/mac/poly1305/poly1305_test.c libtomcrypt/src/mac/xcbc/xcbc_done.c libtomcrypt/src/mac/xcbc/xcbc_file.c \
    libtomcrypt/src/mac/xcbc/xcbc_init.c libtomcrypt/src/mac/xcbc/xcbc_memory.c libtomcrypt/src/mac/xcbc/xcbc_memory_multi.c libtomcrypt/src/mac/xcbc/xcbc_process.c \
    libtomcrypt/src/mac/xcbc/xcbc_test.c libtomcrypt/src/math/fp/ltc_ecc_fp_mulmod.c libtomcrypt/src/math/gmp_desc.c libtomcrypt/src/math/ltm_desc.c \
    libtomcrypt/src/math/multi.c libtomcrypt/src/math/radix_to_bin.c libtomcrypt/src/math/rand_bn.c libtomcrypt/src/math/rand_prime.c \
    libtomcrypt/src/math/tfm_desc.c libtomcrypt/src/misc/adler32.c libtomcrypt/src/misc/base64/base64_decode.c libtomcrypt/src/misc/base64/base64_encode.c \
    libtomcrypt/src/misc/burn_stack.c libtomcrypt/src/misc/compare_testvector.c libtomcrypt/src/misc/crc32.c libtomcrypt/src/misc/crypt/crypt.c \
    libtomcrypt/src/misc/crypt/crypt_argchk.c libtomcrypt/src/misc/crypt/crypt_cipher_descriptor.c libtomcrypt/src/misc/crypt/crypt_cipher_is_valid.c libtomcrypt/src/misc/crypt/crypt_constants.c \
    libtomcrypt/src/misc/crypt/crypt_find_cipher.c libtomcrypt/src/misc/crypt/crypt_find_cipher_any.c libtomcrypt/src/misc/crypt/crypt_find_cipher_id.c libtomcrypt/src/misc/crypt/crypt_find_hash.c \
    libtomcrypt/src/misc/crypt/crypt_find_hash_any.c libtomcrypt/src/misc/crypt/crypt_find_hash_id.c libtomcrypt/src/misc/crypt/crypt_find_hash_oid.c libtomcrypt/src/misc/crypt/crypt_find_prng.c \
    libtomcrypt/src/misc/crypt/crypt_fsa.c libtomcrypt/src/misc/crypt/crypt_hash_descriptor.c libtomcrypt/src/misc/crypt/crypt_hash_is_valid.c libtomcrypt/src/misc/crypt/crypt_inits.c \
    libtomcrypt/src/misc/crypt/crypt_ltc_mp_descriptor.c libtomcrypt/src/misc/crypt/crypt_prng_descriptor.c libtomcrypt/src/misc/crypt/crypt_prng_is_valid.c libtomcrypt/src/misc/crypt/crypt_prng_rng_descriptor.c \
    libtomcrypt/src/misc/crypt/crypt_register_all_ciphers.c libtomcrypt/src/misc/crypt/crypt_register_all_hashes.c libtomcrypt/src/misc/crypt/crypt_register_all_prngs.c libtomcrypt/src/misc/crypt/crypt_register_cipher.c \
    libtomcrypt/src/misc/crypt/crypt_register_hash.c libtomcrypt/src/misc/crypt/crypt_register_prng.c libtomcrypt/src/misc/crypt/crypt_sizes.c libtomcrypt/src/misc/crypt/crypt_unregister_cipher.c \
    libtomcrypt/src/misc/crypt/crypt_unregister_hash.c libtomcrypt/src/misc/crypt/crypt_unregister_prng.c libtomcrypt/src/misc/error_to_string.c libtomcrypt/src/misc/hkdf/hkdf.c \
    libtomcrypt/src/misc/hkdf/hkdf_test.c libtomcrypt/src/misc/mem_neq.c libtomcrypt/src/misc/pk_get_oid.c libtomcrypt/src/misc/pkcs5/pkcs_5_1.c \
    libtomcrypt/src/misc/pkcs5/pkcs_5_2.c libtomcrypt/src/misc/pkcs5/pkcs_5_test.c libtomcrypt/src/misc/zeromem.c libtomcrypt/src/modes/cbc/cbc_decrypt.c \
    libtomcrypt/src/modes/cbc/cbc_done.c libtomcrypt/src/modes/cbc/cbc_encrypt.c libtomcrypt/src/modes/cbc/cbc_getiv.c libtomcrypt/src/modes/cbc/cbc_setiv.c \
    libtomcrypt/src/modes/cbc/cbc_start.c libtomcrypt/src/modes/cfb/cfb_decrypt.c libtomcrypt/src/modes/cfb/cfb_done.c libtomcrypt/src/modes/cfb/cfb_encrypt.c \
    libtomcrypt/src/modes/cfb/cfb_getiv.c libtomcrypt/src/modes/cfb/cfb_setiv.c libtomcrypt/src/modes/cfb/cfb_start.c libtomcrypt/src/modes/ctr/ctr_decrypt.c \
    libtomcrypt/src/modes/ctr/ctr_done.c libtomcrypt/src/modes/ctr/ctr_encrypt.c libtomcrypt/src/modes/ctr/ctr_getiv.c libtomcrypt/src/modes/ctr/ctr_setiv.c \
    libtomcrypt/src/modes/ctr/ctr_start.c libtomcrypt/src/modes/ctr/ctr_test.c libtomcrypt/src/modes/ecb/ecb_decrypt.c libtomcrypt/src/modes/ecb/ecb_done.c \
    libtomcrypt/src/modes/ecb/ecb_encrypt.c libtomcrypt/src/modes/ecb/ecb_start.c libtomcrypt/src/modes/f8/f8_decrypt.c libtomcrypt/src/modes/f8/f8_done.c \
    libtomcrypt/src/modes/f8/f8_encrypt.c libtomcrypt/src/modes/f8/f8_getiv.c libtomcrypt/src/modes/f8/f8_setiv.c libtomcrypt/src/modes/f8/f8_start.c \
    libtomcrypt/src/modes/f8/f8_test_mode.c libtomcrypt/src/modes/lrw/lrw_decrypt.c libtomcrypt/src/modes/lrw/lrw_done.c libtomcrypt/src/modes/lrw/lrw_encrypt.c \
    libtomcrypt/src/modes/lrw/lrw_getiv.c libtomcrypt/src/modes/lrw/lrw_process.c libtomcrypt/src/modes/lrw/lrw_setiv.c libtomcrypt/src/modes/lrw/lrw_start.c \
    libtomcrypt/src/modes/lrw/lrw_test.c libtomcrypt/src/modes/ofb/ofb_decrypt.c libtomcrypt/src/modes/ofb/ofb_done.c libtomcrypt/src/modes/ofb/ofb_encrypt.c \
    libtomcrypt/src/modes/ofb/ofb_getiv.c libtomcrypt/src/modes/ofb/ofb_setiv.c libtomcrypt/src/modes/ofb/ofb_start.c libtomcrypt/src/modes/xts/xts_decrypt.c \
    libtomcrypt/src/modes/xts/xts_done.c libtomcrypt/src/modes/xts/xts_encrypt.c libtomcrypt/src/modes/xts/xts_init.c libtomcrypt/src/modes/xts/xts_mult_x.c \
    libtomcrypt/src/modes/xts/xts_test.c libtomcrypt/src/pk/asn1/der/bit/der_decode_bit_string.c libtomcrypt/src/pk/asn1/der/bit/der_decode_raw_bit_string.c libtomcrypt/src/pk/asn1/der/bit/der_encode_bit_string.c \
    libtomcrypt/src/pk/asn1/der/bit/der_encode_raw_bit_string.c libtomcrypt/src/pk/asn1/der/bit/der_length_bit_string.c libtomcrypt/src/pk/asn1/der/boolean/der_decode_boolean.c libtomcrypt/src/pk/asn1/der/boolean/der_encode_boolean.c \
    libtomcrypt/src/pk/asn1/der/boolean/der_length_boolean.c libtomcrypt/src/pk/asn1/der/choice/der_decode_choice.c libtomcrypt/src/pk/asn1/der/generalizedtime/der_decode_generalizedtime.c libtomcrypt/src/pk/asn1/der/generalizedtime/der_encode_generalizedtime.c \
    libtomcrypt/src/pk/asn1/der/generalizedtime/der_length_generalizedtime.c libtomcrypt/src/pk/asn1/der/ia5/der_decode_ia5_string.c libtomcrypt/src/pk/asn1/der/ia5/der_encode_ia5_string.c libtomcrypt/src/pk/asn1/der/ia5/der_length_ia5_string.c \
    libtomcrypt/src/pk/asn1/der/integer/der_decode_integer.c libtomcrypt/src/pk/asn1/der/integer/der_encode_integer.c libtomcrypt/src/pk/asn1/der/integer/der_length_integer.c libtomcrypt/src/pk/asn1/der/object_identifier/der_decode_object_identifier.c \
    libtomcrypt/src/pk/asn1/der/object_identifier/der_encode_object_identifier.c libtomcrypt/src/pk/asn1/der/object_identifier/der_length_object_identifier.c libtomcrypt/src/pk/asn1/der/octet/der_decode_octet_string.c libtomcrypt/src/pk/asn1/der/octet/der_encode_octet_string.c \
    libtomcrypt/src/pk/asn1/der/octet/der_length_octet_string.c libtomcrypt/src/pk/asn1/der/printable_string/der_decode_printable_string.c libtomcrypt/src/pk/asn1/der/printable_string/der_encode_printable_string.c libtomcrypt/src/pk/asn1/der/printable_string/der_length_printable_string.c \
    libtomcrypt/src/pk/asn1/der/sequence/der_decode_sequence_ex.c libtomcrypt/src/pk/asn1/der/sequence/der_decode_sequence_flexi.c libtomcrypt/src/pk/asn1/der/sequence/der_decode_sequence_multi.c libtomcrypt/src/pk/asn1/der/sequence/der_decode_subject_public_key_info.c \
    libtomcrypt/src/pk/asn1/der/sequence/der_encode_sequence_ex.c libtomcrypt/src/pk/asn1/der/sequence/der_encode_sequence_multi.c libtomcrypt/src/pk/asn1/der/sequence/der_encode_subject_public_key_info.c libtomcrypt/src/pk/asn1/der/sequence/der_length_sequence.c \
    libtomcrypt/src/pk/asn1/der/sequence/der_sequence_free.c libtomcrypt/src/pk/asn1/der/sequence/der_sequence_shrink.c libtomcrypt/src/pk/asn1/der/set/der_encode_set.c libtomcrypt/src/pk/asn1/der/set/der_encode_setof.c \
    libtomcrypt/src/pk/asn1/der/short_integer/der_decode_short_integer.c libtomcrypt/src/pk/asn1/der/short_integer/der_encode_short_integer.c libtomcrypt/src/pk/asn1/der/short_integer/der_length_short_integer.c libtomcrypt/src/pk/asn1/der/teletex_string/der_decode_teletex_string.c \
    libtomcrypt/src/pk/asn1/der/teletex_string/der_length_teletex_string.c libtomcrypt/src/pk/asn1/der/utctime/der_decode_utctime.c libtomcrypt/src/pk/asn1/der/utctime/der_encode_utctime.c libtomcrypt/src/pk/asn1/der/utctime/der_length_utctime.c \
    libtomcrypt/src/pk/asn1/der/utf8/der_decode_utf8_string.c libtomcrypt/src/pk/asn1/der/utf8/der_encode_utf8_string.c libtomcrypt/src/pk/asn1/der/utf8/der_length_utf8_string.c libtomcrypt/src/pk/dh/dh.c \
    libtomcrypt/src/pk/dh/dh_check_pubkey.c libtomcrypt/src/pk/dh/dh_export.c libtomcrypt/src/pk/dh/dh_export_key.c libtomcrypt/src/pk/dh/dh_free.c \
    libtomcrypt/src/pk/dh/dh_generate_key.c libtomcrypt/src/pk/dh/dh_import.c libtomcrypt/src/pk/dh/dh_set.c libtomcrypt/src/pk/dh/dh_set_pg_dhparam.c \
    libtomcrypt/src/pk/dh/dh_shared_secret.c libtomcrypt/src/pk/dsa/dsa_decrypt_key.c libtomcrypt/src/pk/dsa/dsa_encrypt_key.c libtomcrypt/src/pk/dsa/dsa_export.c \
    libtomcrypt/src/pk/dsa/dsa_free.c libtomcrypt/src/pk/dsa/dsa_generate_key.c libtomcrypt/src/pk/dsa/dsa_generate_pqg.c libtomcrypt/src/pk/dsa/dsa_import.c \
    libtomcrypt/src/pk/dsa/dsa_make_key.c libtomcrypt/src/pk/dsa/dsa_set.c libtomcrypt/src/pk/dsa/dsa_set_pqg_dsaparam.c libtomcrypt/src/pk/dsa/dsa_shared_secret.c \
    libtomcrypt/src/pk/dsa/dsa_sign_hash.c libtomcrypt/src/pk/dsa/dsa_verify_hash.c libtomcrypt/src/pk/dsa/dsa_verify_key.c libtomcrypt/src/pk/ecc/ecc.c \
    libtomcrypt/src/pk/ecc/ecc_ansi_x963_export.c libtomcrypt/src/pk/ecc/ecc_ansi_x963_import.c libtomcrypt/src/pk/ecc/ecc_decrypt_key.c libtomcrypt/src/pk/ecc/ecc_encrypt_key.c \
    libtomcrypt/src/pk/ecc/ecc_export.c libtomcrypt/src/pk/ecc/ecc_free.c libtomcrypt/src/pk/ecc/ecc_get_size.c libtomcrypt/src/pk/ecc/ecc_import.c \
    libtomcrypt/src/pk/ecc/ecc_make_key.c libtomcrypt/src/pk/ecc/ecc_shared_secret.c libtomcrypt/src/pk/ecc/ecc_sign_hash.c libtomcrypt/src/pk/ecc/ecc_sizes.c \
    libtomcrypt/src/pk/ecc/ecc_test.c libtomcrypt/src/pk/ecc/ecc_verify_hash.c libtomcrypt/src/pk/ecc/ltc_ecc_is_valid_idx.c libtomcrypt/src/pk/ecc/ltc_ecc_map.c \
    libtomcrypt/src/pk/ecc/ltc_ecc_mul2add.c libtomcrypt/src/pk/ecc/ltc_ecc_mulmod.c libtomcrypt/src/pk/ecc/ltc_ecc_mulmod_timing.c libtomcrypt/src/pk/ecc/ltc_ecc_points.c \
    libtomcrypt/src/pk/ecc/ltc_ecc_projective_add_point.c libtomcrypt/src/pk/ecc/ltc_ecc_projective_dbl_point.c libtomcrypt/src/pk/katja/katja_decrypt_key.c libtomcrypt/src/pk/katja/katja_encrypt_key.c \
    libtomcrypt/src/pk/katja/katja_export.c libtomcrypt/src/pk/katja/katja_exptmod.c libtomcrypt/src/pk/katja/katja_free.c libtomcrypt/src/pk/katja/katja_import.c \
    libtomcrypt/src/pk/katja/katja_make_key.c libtomcrypt/src/pk/pkcs1/pkcs_1_i2osp.c libtomcrypt/src/pk/pkcs1/pkcs_1_mgf1.c libtomcrypt/src/pk/pkcs1/pkcs_1_oaep_decode.c \
    libtomcrypt/src/pk/pkcs1/pkcs_1_oaep_encode.c libtomcrypt/src/pk/pkcs1/pkcs_1_os2ip.c libtomcrypt/src/pk/pkcs1/pkcs_1_pss_decode.c libtomcrypt/src/pk/pkcs1/pkcs_1_pss_encode.c \
    libtomcrypt/src/pk/pkcs1/pkcs_1_v1_5_decode.c libtomcrypt/src/pk/pkcs1/pkcs_1_v1_5_encode.c libtomcrypt/src/pk/rsa/rsa_decrypt_key.c libtomcrypt/src/pk/rsa/rsa_encrypt_key.c \
    libtomcrypt/src/pk/rsa/rsa_export.c libtomcrypt/src/pk/rsa/rsa_exptmod.c libtomcrypt/src/pk/rsa/rsa_free.c libtomcrypt/src/pk/rsa/rsa_get_size.c \
    libtomcrypt/src/pk/rsa/rsa_import.c libtomcrypt/src/pk/rsa/rsa_import_pkcs8.c libtomcrypt/src/pk/rsa/rsa_import_x509.c libtomcrypt/src/pk/rsa/rsa_make_key.c \
    libtomcrypt/src/pk/rsa/rsa_set.c libtomcrypt/src/pk/rsa/rsa_sign_hash.c libtomcrypt/src/pk/rsa/rsa_sign_saltlen_get.c libtomcrypt/src/pk/rsa/rsa_verify_hash.c \
    libtomcrypt/src/prngs/chacha20.c libtomcrypt/src/prngs/fortuna.c libtomcrypt/src/prngs/rc4.c libtomcrypt/src/prngs/rng_get_bytes.c \
    libtomcrypt/src/prngs/rng_make_prng.c libtomcrypt/src/prngs/sober128.c libtomcrypt/src/prngs/sprng.c libtomcrypt/src/prngs/yarrow.c \
    libtomcrypt/src/stream/chacha/chacha_crypt.c libtomcrypt/src/stream/chacha/chacha_done.c libtomcrypt/src/stream/chacha/chacha_ivctr32.c libtomcrypt/src/stream/chacha/chacha_ivctr64.c \
    libtomcrypt/src/stream/chacha/chacha_keystream.c libtomcrypt/src/stream/chacha/chacha_setup.c libtomcrypt/src/stream/chacha/chacha_test.c libtomcrypt/src/stream/rc4/rc4_stream.c \
    libtomcrypt/src/stream/rc4/rc4_test.c libtomcrypt/src/stream/sober128/sober128_stream.c libtomcrypt/src/stream/sober128/sober128_test.c
include $(BUILD_STATIC_LIBRARY)

# ---------------------------------------------------------------------------
# dropbear server
# ---------------------------------------------------------------------------
include $(CLEAR_VARS)
LOCAL_MODULE := aera_dropbear
LOCAL_MODULE_STEM := dropbear
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_C_INCLUDES := $(DROPBEAR_INCLUDES)
LOCAL_CFLAGS := $(DROPBEAR_CFLAGS)
LOCAL_LDFLAGS := -Wl,-z,now -Wl,-z,relro
LOCAL_STATIC_LIBRARIES := libaera_tomcrypt libaera_tommath
LOCAL_SRC_FILES := \
    src/dbutil.c src/buffer.c src/dbhelpers.c src/dss.c \
    src/bignum.c src/signkey.c src/rsa.c src/dbrandom.c \
    src/queue.c src/atomicio.c src/compat.c src/fake-rfc2553.c \
    src/ltc_prng.c src/ecc.c src/ecdsa.c src/sk-ecdsa.c \
    src/crypto_desc.c src/curve25519.c src/ed25519.c src/sk-ed25519.c \
    src/dbmalloc.c src/dbctype.c src/gensignkey.c src/gendss.c \
    src/genrsa.c src/gened25519.c src/svr-kex.c src/svr-auth.c \
    src/sshpty.c src/svr-authpasswd.c src/svr-authpubkey.c src/svr-authpubkeyoptions.c \
    src/svr-session.c src/svr-service.c src/svr-chansession.c src/svr-runopts.c \
    src/svr-agentfwd.c src/svr-main.c src/svr-x11fwd.c src/svr-forward.c \
    src/svr-tcpfwd.c src/svr-streamfwd.c src/svr-authpam.c src/common-session.c \
    src/packet.c src/common-algo.c src/common-kex.c src/common-channel.c \
    src/common-chansession.c src/termcodes.c src/loginrec.c src/tcp-accept.c \
    src/listener.c src/process-packet.c src/dh_groups.c src/common-runopts.c \
    src/circbuffer.c src/list.c src/netio.c src/chachapoly.c \
    src/gcm.c src/kex-x25519.c src/kex-dh.c src/kex-ecdh.c \
    src/kex-pqhybrid.c src/sntrup761.c src/mlkem768.c src/aera_auth.c
include $(BUILD_EXECUTABLE)
