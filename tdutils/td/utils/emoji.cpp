//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/emoji.h"

#include "td/utils/base64.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/Gzip.h"

namespace td {

bool is_emoji(Slice str) {
  constexpr size_t MAX_EMOJI_LENGTH = 38;
  static const FlatHashSet<Slice, SliceHash> emojis = [max_emoji_length = MAX_EMOJI_LENGTH] {
#if TD_HAVE_ZLIB
    Slice packed_emojis(
        "eJyVvWly20qztTuVL-L7d36dvpmdTcuSbXUm1YKSLDcwIQmSTImSqAZNxB3KewdwdsSdwN1YWAlkVpIQFHGwnE9VViaYVQBYoM5-_7Ex_j__"
        "2Bj_7-P23_"
        "8cVQfM7fPqqM2L6qjNtDpq87I6anNaHbU5qw6YB3l11GZRHTAPd6ujNveqA2b0qTpq83N11OaX6qjN9eqozY3qqM3N6qjNreqoTRy1-"
        "bU6anNYHbU5qo7a3KmO2iyrA-"
        "Z4pzpq80d11OZFddRmWh21mVdHbRbVAfNopTpq82N11OZWddTmbnXU5kV11OZtddTmrDpq8746avO5Omozrw6Yxx-roza_"
        "VEdtrldHbZ5VB8yTjeqoza3qqM2d6qjN3eqozb3qqM2D6oD5ba86anO_OmrzoDpqc1odtVlWB8zLo-qozePqgHn1tTpqc686KvOv03crkBo-"
        "1FLD6haEMIQQRhDCDoSwCyHsQQj7EMIBhHAIIUQQwhhSw6f3EMIYQvgDIdxCCDMI4Q5CuIcQHiCERwjhCUJ4htTw-"
        "SuEMITUsPEOQngPIQwghA8QwgqE8BFCWIUQ1iCETxDCZwjhC4SwDiFsQAibEMIWhFAL4SuEMIQQRhDCDoSwCyHsQQj7EMIBhHAIIUQQwhhCOII"
        "QjiGEEwjhG4RwCiF8hxCuIIRrCOEPhDCFEG4ghFsIYQYh3EEI9xDCHEJ4hBCeIIRnCOEFQsgghBxCKCCEElLD5jsI4T2EMIAQPkAIKxDCRwhhF"
        "UJYgxA-"
        "QQifIYQvEMI6hLABIWxCCFsQQi2ErxDCEEIYQQg7EMIuhLAHIexDCAcQwiGEEEEIYwjhCEI4hhBOIIRvEMIphPAdQvgBIfyEEH5BCDGE8BtCmE"
        "AICYRwBiGcQwgXEEIKIVxCCFcQwjWE8AdCmEIINxDCLYQwgxDuIIR7COEBQphDCI8QwhOE8AwhvEAIGYRQQAglpIatdxDCewhhACF8gBBWIISP"
        "EMIqhLAGIXyCED5DCF8ghHUIYQNC2IQQtiCEWghfIYQhhDCCEHYghO8Qwg8I4SeE8AtCiCGE3xDCBEJIIIQzCOEcQriAEFII4RJCuIIQriGEPx"
        "DCFEK4gRBuIYQZhHAHIdxDCA8QwhxCeIQQniCEZwjhBULIIIQcQigghBJSw_"
        "Y7COE9hDCAED5ACCsQwkcIYRVCWIMQPkEInyGELxBCLYSvEMIQQhhBCDsQwncI4QeE8BNC-"
        "AUhxBDCbwhhAiEkEMIZhHAOIVxACCmEcAkhXEEI1xDCHwhhCiHcQQiPEMIThPAMIbxACBmEkEMIBYRQQmr4-"
        "g5CeA8hDCCEDxDCCoTwEUJYhRDWIIRPEMJnCOELhLAOIWxACJsQwhaEUAvhK4QwhBBGEMIOhLALIexBCPsQwgGEcAghRBDCGEI4ghCOIYQTCOE"
        "bhHAKIXyHEH5ACD8hhF8QQgwh_IYQJhBCAiGcQQjnEMIFhJBCCJcQwhWEcA0h_"
        "IEQphDCDYRwCyHMIIQ7COEeQniAEOYQwiOE8AQhPEMILxBCBiHkEEIBqWH4DkIYQAgfIIQVCOEjhLAKIaxBCJ8ghM8QwhcIYR1C2IAQNiGELQi"
        "hFsJXCGEIIYwghB0IYRdC2IMQ9iGEAwjhEEKIIIQxhHAEIRxDCCcQwjcI4RRC-A4h_"
        "IAQfkIIvyCEGEL4DSFMIIQEQjiDEM4hhAsIIYUQLiGEKwjhGkL4AyFMIYQbCOEWQphBCHcQwj2E8AAhzCGERwjhCUJ4hhBeIIQMQsghhAJCKCE"
        "1jN5BCO8hhAGE8AFCWIEQPkIIqxDCGoTwCUL4DCF8gRDWIYQNCGETQtiCEGohfIUQhhDCCELYgRB2IYQ9CGEfQjiAEA4hhAhCGEMIRxDCMYRwA"
        "iF8gxBOIYTvEMIPCOEnhPALQoghhN8QwgRCSCCEMwjhHEK4gBBSCOESQriCEK4hhD8QwhRCuIEQbiGEGYRwByHcQwgPEMIcQniEEJ4ghGcI4QV"
        "CyCCEHEIoIIQSUsPOOwjhPYQwgBA-"
        "QAgrEMJHCGEVQliDED5BCJ8hhC8QwjqEsAEhbEIIWxBCLYSvEMIQQhhBCDsQwi6EsAch7EMIBxDCIYQQQQhjCOEIQjiGEE4ghG8QwimE8B1C-"
        "AEh_"
        "IQQfkEIMYTwG0KYQAgJhHAGIZxDCBcQQgohXEIIVxDCNYTwB0KYQgg3EMIthDCDEO4ghHsI4QFCmEMIjxDCE4TwDCG8QAgZhFBCath9ByG8hxA"
        "GEMIHCGEFQvgIIaxCCGsQwicI4TOE8AVCWIcQNiCETQhhC0KohfAVQhhCCCMIYQdC2IUQ9iCEfQjhAEI4hBAiCGEMIRxBCMcQwgmE8A1COIUQv"
        "kMIPyCEnxDCLwghhhB-QwgTCCGBEM4ghHMI4QJCSCGESwjhCkK4hhD-"
        "QAhTCOEGQriFEGYQwh2EcA8hPEAIcwjhEUJ4ghCeIYQXCCGDEHJIDXvrEMIGhLAJIWxBCF8hhCGEMIIQdiCEXQhhD0LYhxAOIIRDCCGCEMYQwh"
        "GEcAwhnEAI3yCEUwjhO4TwA0L4CSH8ghBiCOE3hDCBEBII4RlSw_"
        "4ehLAPIcSQGg5eIIQMQsghhAJCKCE1HL6DEN5DCAMI4QOEsAIhfIQQViGENQjhE4TwGUL4AiGsQwgbEMImhLAFIdRC-"
        "AohDCGEEYSwAyHsQgh7EMI-"
        "hHAAIRxCCBGEMIYQjiCEYwjhBEL4BiGcQgjfIYQfEMJPCOEXhBBDCL8hhAmEkEAIZxDCOYRwASGkEMIlhHAFIVxDCH8ghCmEcAMh3EIIMwjhDk"
        "K4hxAeIIQ5hPAIITxBCM8QwguEkEEIOYRQQAglpIboHYTwHkIYQAgfIIQVCOEjhLAKIaxBCJ8ghM8QwhcIYR1C2IAQNiGELQihlhrG7yCE9xDC"
        "AEL4ACGsQAgfIYRVCGENQvgEIXyGEL5ACOsQwgaEsAkhbEEItRC-"
        "QghDCGEEIexACLsQwh6EsA8hHEAIhxBCBCGMIYQjCOEYQjiBEL5BCKcQwncI4QeE8BNC-"
        "AUhxBDCbwhhAiEkEMIZhHAOIVxACCmEcAkhXEEI1xDCHwhhCiHcQAi3EMIMQriDEO4hhAcIYQ4hPEIITxDCM4TwAiFkEEIOIRQQQgmp4egdhPA"
        "eQhhACB8ghBUI4SOEsAEhfIUQhhDCCELYgxD2IYQDCOEYQjiBEL5BCKcQQgohXEIIdxDCPYTwACHMIYRHCOEJQniGEF4ghAxSw-"
        "l3COEHhPATQvgFIcQQwm8IYQIhJBDCGYRwDiFcQAgphDCF1BBvQAibEMIWhFAL4SuEMIQQRhDCDoSwCyHsQQj7EMIBhHAIIUQQwhhCOIIQjiGE"
        "EwjhG4RwCiF8hxB-QAg_"
        "IYRfEEIMIfyGECYQQgIhnEEI5xDCBYSQQgiXEMIVhHANIfyBEKYQwg2EcAshzCCEOwjhHkJ4gBDmEMIjhPAEITxDCBmEkEMIBYRQQmr4_"
        "Q5CeA8hDCCEDxDCCoTwEUJYgxA-"
        "QQifIYQvEMI6hLABIWxCCFsQQi2ErxDCEEIYQQg7EMIuhLAHIexDCAcQwiGEEEEIYwjhCEI4hhBOIIRvEMIphPAdQvgBIfyEEH5BCDGE8BtCmE"
        "AICYRwBiGcQwgXEEIKIVxCCFcQwjWE8AdCmEIINxDCLYQwgxDuIIR7COEBQphDCI8QwhOE8AwhvEAIGYSQQwgFhFBCapi8gxDeQwgDCOEDhLAC"
        "IXyEEFYhhDUI4ROE8BlC-AIhrEMIGxDCJoSwBSHUQvgKIQwhhBGEsAMh7EIIexDCPoRwACEcQggRhDCGEI4ghGMI4QRC-"
        "AYhnEII3yGEHxDCTwjhF4QQQwi_IYQJhJBACGcQwjmEcAEhpBDCJYRwBSFcQwh_"
        "IIQphHADIdxCCDMI4Q5CuIcQHiCEOYTwCCE8QQjPEMILhJBBCDmEUEAIJaSG5B2E8B5CGEAIHyCEFQjhI4SwCiGsQQifIITPEMIXCGEdQtiAED"
        "YhhC0IoRbCVwhhCCGMIIQdCGEXQtiDEPYhhAMI4RBCiCCEMYRwBCEcQwgnEMI3COEUQvgOIfyAEH5CCL8ghBhC-"
        "A0hTCCEBEI4gxDOIYQLCCGFEC4hhCsI4RpC-"
        "AMhTCGEGwjhFkKYQQh3EMI9hPAAIcwhhEcI4QlCeIYQXiCEDELIIYQCQighNZxPIYQbCOEWQphBCHcQwj2E8AAhzCGERwjhCUJ4hhBeIIQMUsP"
        "FOwjhPYQwgBA-"
        "QAgrEMJHCGEVQliDED5BCF8hhCGEMIIQdiCEXQhhD0LYhxAOIIRDCCGCEMYQwhGEcAwhnEAI3yCEUwjhO4TwA0L4CSH8ghBiCOE3hDCBEBII4Q"
        "xCOIcQLiCEFEK4hBCuIIRrCOEPhDCFEG4ghFsIYQYh3EEI9xDCA4QwhxAeIYQnCOEZQniBEDIIIYcQSkgN6TsI4T2EMIAQPkAIKxDCRwhhC0Ko"
        "hfAVQhhCCCMIYQdC2IUQ9iCEfQjhAEI4hBAiCGEMIRxBCN8hhB8Qwk8I4ReEEEMIvyGECYSQQAhnEMIUQriBEG4hhBmEcAch3EMIDxDCHEJ4hF"
        "Tw_5z__X8wrv_-v8r4x7usOmC-_1wdMFd-VkdtPlUHzNXd6qjNveqozf3qqM2D6qjNw-"
        "qozag6avO8OmrzojpgbpxVB8xtHLV5VR21eV0dtfmnOmrzpjpq87Y6avOxOmrzqTpq87k6YO4MqgPm_kV11GZaHbX5UB0wD95VR22-"
        "VEdtZtUB8_BdddTm--qozUF11OaH6qjNleqoza3qqM1hddTmYXXU5kl11Ob36qjNn9VRm7-qozYn1VGbF9VRm9fVUZt_qqM2H6ujNp-"
        "qozafqwNm9K46anNQHbV5Wh21-b06avNXddTm7-qozUl11OZZddTmS3XUZlEdMMej6qjN3eqozb3qqM396qjNg-"
        "qozag6avOoOmrzuDpq83t11GZSHbU5rY7avKkOmEefqqM2cdTmsDpqc6c6avO8OmpzWh21eVMdtXlXHbU5r47afKyO2nyqDpjHg-qozU_"
        "VUZufq6M2N6qjNjerozZx1OaoOmpztzpqc786avOkOmrzR3XU5qw6avOuOmCerFRHba5VR23-"
        "qo7ajKsD5rcf1QEzvquO2ryvDpiXH6ujNlerozbXqqMy_9930-qozbw6YH45qI7ajKqjvoV9nEIINxBCASGUkBo-DSCEOaSGjR8QQgwh_"
        "IYQJhBCAiGcQQjnEMIFhJBCCJcQwgOkhs0cUsPWPoRwACFEEMIYQjiCEL5BCKeQGrbXIYQNCGETQtiCEHYhhD0IYR9COIAQDiGECEIYQwhHEMI"
        "xhHACIXyDEE4hhBmEcA8hzCE1fC0hNQzfQ2rYySE17H2GEL5ACH8ghCmEMIMQ7iCEewjhAUKYQwiPEMITpIb9NQjhC4SwDiFsQAibEMJXCOE3h"
        "HAGIdxACLcQQgap4WAAIXyAEFYghCGEMIIQdiCEYwjhBEL4BiH8gBB-QQhnEMIfCGEGITxDajhahxA2IYQtCKEWwncI4QeE8BNC-"
        "AUhxBDCbwjhHEKYQggzSAX_9x8ffv2f__u3pf6t2v-p4n9i-z-17f9c8T-z_Z_b9n-p-F_Y_i9t-79W_K9s_9e2_d8q_je2_1vb_u8V_zvb_"
        "71t_4-K_4Pt_9G2_2fF_8n2_2zb_6vi_2L7f7Xt_13xf7P9v9v2_6n4f9j-P237X6drk7-"
        "Ps8ZQjeeNoRovGkM1po2hGi8bQzVeN4ZqvGkM1XjbGKrxrjFU40NjqMZ5Y6jGx8ZQjU-"
        "NoRqfG0M1Zo2hGvPGUI1lY7AxqbgxVGPSGKrxvDFU40VjqMa0MVTjZWOoxqvGUI3XjaEa_"
        "zSGarxpDNV42xiqcdYYqvGuMVTjQ2OoxnljqMbHxlCNT42hGl8aQzVmjaEai8ZQjWVjsPFMJu5MTVxlnzWGajxvDNWYNoZqvGwM1XjVGKrxujF"
        "U47QxVONNY6jG28ZQjbPGUI13jaEa7xtDNc4bQzU-"
        "N4ZqfGkM1Zg1hmrMG0M1Fo2hGsvGYOP5X7w6YKjGy8ZQjX8aQzVOG0M13jaGarxrDNVYNgYbL2TZXKhlcyHL5kItmws5-"
        "Qt18hdy8hfq5C9khVyoFXIh03GhpqOyHxtDNT41hmp8bgw2prLqUrXqUildqkqXSulSVbpUSpeq0qVSulSVLpWTT9XJX0rpLlXpKjtpDNV43hi"
        "q8aIxVGPaGKrxsjFU41VjqMbrxlCNN42hGm8bQzXOGkM13jeGanxoDNU4bwzV-NgYqvGpMVTjc2OoxqwxVGPRGGy8kim-UlN8JR_zSn3MK_"
        "mYV-pjXsnJX6mTv5LzvFLneSXneaXO8_ovXjIwVON5Y6jGi8ZQjTeNoRpvG0M1zhpDNd41hmp8aAzVOG8M1fjYGKrxqTHY-EdO_o86-T9ynn_"
        "Uef6RU_qjTumPrKU_ai1NJeZUxazsy8ZQjVeNoRqvG0M13jaGapw1hmq8bwzVOG8M1Zg1hmosGkM1lo3Bxpu_"
        "eLuAoRqTxlCNZ42hGq8bQzVOG0M1zhtDNT42hmp8agzV-NwYqvGlMVRj0RhsvJWPeas-"
        "5q18olv1iSr7vDFU40VjqMa0MVTjZWOoxqvGUI3TxlCNN42hGm8bQzXOGkM13jWGarxvDNX40Biqcd4YqvGxMVTjU2OoxufGUI0vjaEas8ZQjX"
        "ljqMaiMVRj2RhsnMkUz9QUz2SKZ2qKZzKbMzWbM5nNmZrNmczmTM1mZV83hmq8aQzVeNcYqvG-"
        "MVTjvDFU43NjqMayMdh4JyvkTq2QeynIvSrIvXz2e_XZ7-Wz36vPfi-f_V599sq-"
        "agzVOG0M1XjTGKrxtjFU46wxVOO8MVTjY2OoxqfGUI1ZY6jGojHY-"
        "CBVelBVmkuV5qpKc5nNuZrNuZzSXJ3SXCZuriZuLqc0V6f0KNkfVfbKThpDNZ41hmo8bwzVeNEYqvGyMVTjVWOoxuvGUI1_"
        "GkM1ThtDNd40hmq8bQzVOGsM1XjXGKpx3hiq8bExVONTY6jGl8ZQjXljqMaiMVRj2RhsfJKJe1IT9yRz9KTm6Enm6EnNUWWnjaEaLxtDNV41hm"
        "r80xiqcdoYqvGmMVTjbWOoxlljqMa7xlCN88ZQjU-NoRpfGkM1Zo2hGsvGYOOzFPlZFflZqvSsqvQsn-hZfaJn-"
        "UTP6hM9y7J5VsvmWeb9Wc37s5zSszqlFzmlF3VKLzLvL2reK_"
        "uiMVTjZWOoxuvGUI2zxlCNz43BxkzWUqbWUiYfM1MfM5cVkqsVUsh5Fuo8C5nNQs1mKZ-9VJ-9lMqXqvKlTHHZTvH23f_3_X3y9_Hz7-"
        "P338c1uezsXDB49vfx6-_jbtFg07lg8Pzv45J-brDprAb_4_AE_2E3_ts2ZfJv25TLv21TIf-2TaX8i6ajpzp8_W_blMm_bVMu_"
        "7ZNhfzbNpXyL5qOv9Th63_bpkz-bZty-bdtKuTftqmUf-umdYZfb8OvM_x6G36d4dfb8OsMv96GX2f49Tb8BsNvtOE3GH6jDb_B8Btt-"
        "A2G32jDbzD8Rht-k-E32_CbDL_Zht9k-M02_CbDb7bhNxl-s_3P-W195H8TsDZUY9YYqjFvDNVYNIZqLBuD63sg__"
        "HBgfoPEA7kP0I4UP8hwgET1YZqLBpDNZaNwcYPkuiDSvRBEn1QiT5Iog8q0QdJ9EEl-"
        "iCJPqhEK5JoRSVakUQrKtGKJFpRiVYk0YpKtCKJVlSiNUm0phKtSaI1lWhNEq2pRGuSaE0lWpNEayrRF0n0RSX6Iom-"
        "qERfJNEXleiLJPqiEn2RRF9UonVJtK4SrUuidZVoXRKtq0TrkmhdJVqXROsq0YYk2lCJNiTRhkq0IYk2VKINSbShEm1Ioo020VCW91At76Es76"
        "Fa3kNZ3kO1vIeyvIdqeQ9leQ_V8h7K8h6q5T2U5T1Uy3soy3uolvdQlvdQLe-hLO-hWt7DVUm0qhKtSqJVlWhVEq2qRKuSaFUlWpVEqyqRLO-"
        "hWt5DWd5DtbyHsryHankPZXkP1fIeyvIequU9_CSJPqlEnyTRJ5XokyT6pBJ9kkSfVKJPkuiTSvRZEn1WiT5Los8q0WdJ9Fkl-"
        "iyJPqtEnyXRZ5VILtihumCHcsEO1QU7lAt2qC7YoVywQ3XBDuWCHaoLdigX7FBdsEO5YIfqgh3KBTtUF-"
        "xQLtihumCHcsEO1QU7lAt2qC7YoVywQ3XBDuWCHaoLdigX7FBdsEO5YIf6gt2URJsq0aYk2lSJNiXRpkq0KYk2VaJNSaSesMMtSbSlEm1Joi2V"
        "aEsSbalEW5JoSyXakkRbKtG2JNpWibYl0bZKtC2JtlWibUm0rRJtS6JtleirJPqqEn2VRF9Voq-"
        "S6KtK9FUSfVWJvkqiryrRRBJNVKKJJJqoRBNJNFGJJpJoohJNJNFEJUokUaISJZIoUYkSSZSoRIkkSlSiRBIlKtGZJDpTic4k0ZlKdCaJzlSiM"
        "0l0phKdSaIzlehcEp2rROeS6FwlOpdE5yrRuSQ6V4nOJdG5SpRKolQlSiVRqhKlkihViVJJlKpEqSRKVaJLSXSpEl1KokuV6FISXapEl5LoUiW"
        "6lESXKtGVJLpSia4k0ZVKdCWJrlSiK0l0pRJdSaIrlehaEl2rRNeS6FolupZE1yrRtSS6VomuJdG1SjSVRFOVaCqJpirRVBJNVaKpJJqqRFNJN"
        "FWJbiTRjUp0I4luVKIbSXSjEt1IohuV6EYS3ahEt5LoViW6lUS3KtGtJLpViW4l0a1KdCuJblWimSSaqUQzSTRTiWaSaKYSzSTRTCWaSaKZSnQ"
        "nie5UojtJdKcS3UmiO5XoThLdqUR3kuhOJbqXRPcq0b0kuleJ7iXRvUp0L4nuVaJ7SXSvEj1IogeV6EESPahED5LoQSV6kEQPKtGDJHpQieaSa"
        "K4SzSXRXCWaS6K5SjSXRHOVaC6J5irRoyR6VIkeJdGjSvQoiR5VokdJ9KgSPUqiR5Uok0SZSpRJokwlyiRRphJlkihTiTJJlLWJRu-"
        "ZqDZUY9YYqjFvDNVYNIZqLBuDjbI_Gqn90Uj2RyO1PxrJ_mik9kcj2R-N1P5oJPujkdofjWR_NFL7o5Hsj0ZqfzSS_"
        "dFI7Y9Gsj8aqf3RSPZHI7U_"
        "Gsmbk5F6czKSNycj9eZkJG9ORurNyUjenIzUm5ORvDkZqTcnI9mIjdRGbCQbsZHaiI1kIzZSG7GRbMRGaiM2ko3YSG3ERrIRG6mN2Eg2YiO1ER"
        "vJRmykNmIj2YiN1EZsJBuxkdqIjeSb6kh9Ux3JN9WR-qY6km-qI_VNdSTfVEfqm-"
        "pIvqmO1DfV0VASDVWioSQaqkRDSTRUiYaSaKgSDSXRUCW6kEQXKtGFJLpQiS4k0YVKdCGJLlSiC0l00Sbak8fEnnpM7MljYk89JvbkMbGnHhN7"
        "8pjYU4-JPXlM7KnHxJ48JvbUY2JPHhN76jGxJ4-"
        "JPfWY2JPHxJ56TOzJY2JPPSb2niXRs0r0LImeVaJnSfSsEj1LomeV6FkSPbeJ9r8yUW2oxqwxVGPeGKqxaAzVWDYGG_"
        "ck0Z5KtCeJ9lSiPUm0pxLtSaI9lWhPEu2pRPuSaF8l2pdE-yrRviTaV4n2JdG-"
        "SrQvifbbRJHc6yJ1r4vkXhepe10k97pI3esiuddF6l4Xyb0uUve6SO51kbrXRXKvi9S9LpJ7XaTudZHc6yJ1r4vkXhepe10k97pI3esiuddF6l"
        "4Xyb0uUve6SO51kbrXRXKvi9S9LpI3J5F6cxLJm5NIvTmJ5M1JpN6cRPLmJFJvTiJ5cxKpNyeRvDmJ1JuTSN6cROrNSSRvTiL15iSSNyeRenMS"
        "yZuTSL05ieTNSaTenETy5iRSb04ieXMSqTcnkbw5idSbk0jenETqzUkkb04i9eYkkjcnkXpzEsmbk0i9OYnkzUmk3pxE8uYkUm9OInkeRep5FM"
        "nzKFLPo0ieR5F6HkXyPIrU8yiS51GknkfjX0xUG6oxawzVmDeGaiwaQzWWjcFGeUyM1WNiLI-"
        "JsXpMjOUxMVaPibE8JsbqMTGWx8RYPSbG8pgYq8fEWB4TY_"
        "WYGMtjYqweE2N5TIzVY2Isj4mxekyMZTcxVruJsewmxmo3MZbdxFjtJsaymxir3cRYdhNjtZs4esdEtaEas8ZQjXljqMaiMVRj2RhslAv2SF2w"
        "R3LBHqkL9kgu2CN1wR7JBXukLtgjuWCP1AUbS6JYJYolUawSxZIoVoliSRSrRLEkinUiuY5idR3Fch3F6jqK5TqK1XUUy3UUq-"
        "solusoVtdRfCiJDlWiQ0l0qBIdSqJDlehQEh2qRIeS6FAliiRRpBJFkihSiSJJFKlEkSSKVKJIEkUq0VgSjVWisSQaq0RjSTRWicaSaKwSjSXR"
        "WCU6kkRHKtGRJDpSiY4k0ZFKdCSJjlSiI0l0pBIdS6JjlehYEh2rRMeS6FglOpZExyrRsSQ6VolOJNGJSnQiiU5UohNJdKISnUiiE5XoRBKdqE"
        "TfJNE3leibJPqmEn2TRN9Uom-S6JtK9E0SfVOJTiXRqUp0KolOVaJTSXSqEp1KolOV6FQSnapE8oI9Vi_YY3nBHqsX7LG8YI_VC_"
        "ZYXrDH6gV7LC_YY_WCPZ5KoqlKNJVEU5VoKommKtFUEk1VoqkkmqpE8gYyVm8gY3kDGas3kLG8gYzVG8hY3kDG6g1kLG8gY_"
        "UGMpY3kLF6AxnLG8hYvYGM5Q1krN5AxvIGMlZvIGN5AxmrN5CxvIGM1RvIWN5AxuoNZCxvIGP1BjKWN5CxegMZyxvIWL2BjOU7Q6y-"
        "M8TynSFW3xli-c4Qq-8MsXxniNV3hli-"
        "M8TqO0Ms3xli9Z0hlu8MsfrOEMt3hlh9Z4jlO0OsvjPE8p0hVt8ZYvnOEKvvDLF8Z4jVd4ZYvjPE6jtDLN8ZYvWdIZbvDLH6zhDLG8hYvYGM5Q"
        "1krN5AxvIGMlZvIGN5AxmrN5CxvIGM1RvIWN5AxuoNZCxvIGP1BjKWN5CxegMZyxvIWL2BjOUNZKzeQMZPkuhJJXqSRE8q0ZMkelKJniTRk0r0"
        "JImeVKJcEuUqUS6JcpUol0S5SpRLolwlyiVRrhIVkqhQiQpJVKhEhSQqVKJCEhUqUSGJijbRb1kMv9Vi-C2L4bdaDL9lMfxWi-"
        "G3LIbfajH8lsXwWy2GiVxHE3UdTeQ6mqjraCLX0URdRxO5jibqOprIdTRR19FErqOJuo4mch1N1HU0ketooq6jiVxHE3UdTeQ6mqjraCLLe6KW"
        "90SW90Qt74ks74la3hNZ3hO1vCeyvCdqeU9keU_U8p7I8p6o5T2R5T1Ry3siy3uilvdElvdELe_"
        "JiyR6UYleJNGLSvQiiV5UohdJ9KISvUiilzZRIrvyRO3KE9mVJ2pXnsiuPFG78kR25YnalSeyK0_"
        "UrjyRXXmiduWJ7MoTtStPZFeeqF15IrvyRO3KE9mVJ2pXnshuIlG7iUR2E4naTSSym0jUbiKR3USidhOJ7CYStZtI5C1xot4SJ_"
        "KWOFFviRN5S5yot8SJvCVO1FviRN4SJ-"
        "otcTKSRCOVaCSJRirRSBKNVKKRJBqpRCNJNFKJdiTRjkq0I4l2VKIdSbSjEu1Ioh2VaEcS7ahEu5JoVyXalUS7KtGuJNpViXYl0a5KtCuJdlUi"
        "eaeaqHeqibxTTdQ71UTeqSbqnWoi71QT9U41kXeqiXqnmsg71US9U03knWqi3qkm8k41Ue9UE3mnmqh3qom8U03UO9XkQBIdqEQHkuhAJTqQRA"
        "cq0YEkOlCJDiTRgUokm-VEbZYT2SwnarOcyGY5UZvlRDbLidosJ7JZTtRmOZHNcqI2y4lslhO1WU5ks5yozXIim-"
        "VEbZYT2SwnarOcyGY5UZvlRDbLidosJ7JZTtRmOZHNcqI2y4lslhO1WU5ks5yozXIim-"
        "VEbZYT2SwnarOcyGY5UZvlRDbLidosJ7JZTtRmOZHNcqI2y4lslhO1WU5ks5yozXIim-"
        "VEbZYT2SwnarOcyGY5UZvlRDbLidosJ7JZTtRmOZHNcqI2y6n8Dpuq32FT-R02Vb_DpvI7bKp-h03ld9hU_Q6byu-wqfodNpU_"
        "w07Vn2Gn8mfYqfoz7FT-DDtVf4adyp9hp-rPsFP5M-xU_Rl2Kj-CpOpHkFR-BEnVjyCp_AiSqh9BUvkRJFU_"
        "gqTyI0iqfgRJp5JoqhJNJdFUJZpKoqlKNJVEU5VoKommKpFsllO1WU5ls5yqzXIqm-"
        "VUbZZT2SynarOcymY5VZvlVDbLqdosp7JZTtVmOZXNcqo2y6lsllO1WU5ls5yqzXIqm-"
        "VUbZZT2SynarOcymY5VZvlVDbLqdosp7JZTtVmOZXNcqo2y6lsllO1WU5ls5yqzXIqm-VUbZZT2SynarOcypf8VH3JT-VLfqq-5KfyJT9VX_"
        "JT-ZKfqi_5qXzJT9WX_FS-5KfqS34qX_JT9SU_lS_5qfqSn8qX_FR9yU_lS36qvuSnsj9K1f4olf1RqvZHqeyPUrU_SmV_"
        "lKr9USr7o1Ttj1LZTaRqN5HKbiJVu4lUdhOp2k2ksptI1W4ild1Eqv9c5-wf7zb_cRL_rX_J_-R73fJ3f1fjguGj9eUDevWpmH_"
        "xf5le7KBrgcdCx0TbQZePkSyOsSDUwogLznvx6S__FJ0fZsFnWvzRln_Czg-6_PN2fuxzn-Z8YfzzpR_7vOtjn_vzOe86kQUf-7zrY58v_"
        "djniz92dUWaq4bXqu5pL5RXO7vCZl0juzu7wuZdI7s7u8IWXSO7O7vCll0juzuXhG3uOB2T82afHrmWzdibfXrkWjaNb_"
        "bpkWvZ3L7Zp0euZRP-Zp8g11-"
        "n8cmSWQi6OgfmHV2dA4uOrs6BZUeXGpgtvWVlXbeshZ1dYbOukd2dXWHzrpHdnV1hi66R3Z1dYcuukd2dS8IuvB0FAd7s0yPXshl7s0-"
        "PXMum8c0-PXItm9s3-_TItWzC3-wT5LIX-0tHV-"
        "fAvKOrc2DR0dU5sOzoUgPzpbesvOuWtbCzK2zWNbK7syts3jWyu7MrbNE1sruzK2zZNbK7c0nYhbejIMCbfXrkWjZjb_"
        "bpkWvZNL7Zp0euZXP7Zp8euZZN-"
        "Jt9glxLblkLujoHZh1dnQOLjq7OgWVHlxpYLL1lFV23rIWdXWGzrpHdnV1h866R3Z1dYYuukd2dXWHLrpHdnUvCLrwdBQHe7NMj17IZe7NPj1z"
        "LpvHNPj1yLZvbN_v0yLVswt_sE-Racsta0NU5MOvo6hyYd3R1Diw7utTAcuktq-"
        "y6ZS3s7AqbdY3s7uwKm3eN7O7sClt0jezu7Apbdo3s7lwSduHtKAjwZp8euZbN2Jt9euRaNo1v9umRa9ncvtmnR65lE_"
        "5mnyDXklvWgq7OgVlHV-fAvKOrc2DR0cWB5-aSUi3t1bKwcdHw80WeixsXDDe_aQUDevV1xFx0Er36VMy_-CNI8GvMgp9hFv_-"
        "on94CX5xWfBTy-LfWBb8uLL4VxW2Ljjvxae__FN0fpgFn2nxR1v-CTs_6PLPu-Rj_7XsxyTV4x_"
        "ASzu7wmZdI7s7u8LmXSO7O7vCFl0juzu7wpZdI7s7l4U975qV1zu7wi6dldc7u8IunZXXO7vCLp2V1zu7wi6dldc7l4R134cWBHizT49cy2bsz"
        "T49ci2bxjf79Mi1bG7f7NMj17IJf7PPK7m6Ltw3-_TI9dq89_bpkeu1ee_t0yPXa_Pe26dHrtfmvbdPkGvB1-IlXZ0D846uzoFFR1fnwLKja-"
        "lAt9KCrs6BSz7jotVie5d8xkVTb3uXfMZwHhf__q16Fn_L6vr9e2HYrGtkd2dX2LxrZHdnV9iia2R3Z1fYsmtkd-eysOdds_"
        "J6Z1fYpbPyemdX2KWz8npnV9ils_J6Z1fYpbPyeueSsAu_Qb32Jwuv-fTItWzG3uzTI9eyaXyzT49cy-b2zT49ci2b8Df7vJKr68J9s0-PXK_"
        "Ne2-fHrlem_fePj1yvTbvvX165Hpt3nv7BLkWvLVc0tU5MO_"
        "o6hxYdHR1Diw7upYOdKs66OocuOQzLlotS75lLejqHLjkM4bzuPhPdlTP4m9ZXX-"
        "yszBs1jWyu7MrbN41sruzK2zRNbK7syts2TWyu3NZ2POuWXm9syvs0ll5vbMr7NJZeb2zK-"
        "zSWXm9syvs0ll5vXNJ2IXfoF77K6vXfHrkWjZjb_bpkWvZNL7Zp0euZXP7Zp8euZZN-Jt9XsnVdeG-"
        "2adHrtfmvbdPj1yvzXtvnx65Xpv33j49cr027719glxLvmUt_wu8xb1ZR1fnwKKjq3Ng2dG1dKBb1cu_"
        "ZfneJZ9x0cpc8i1rQVfnwCWfMZzHxX9lqHoWf8vq-"
        "ivDhWGzrpHdnV1h866R3Z1dYYuukd2dXWHLrpHdncvCnnfNyuudXWGXzsrrnV1hl87K651dYZfOyuudXWGXzsrrnUvCLvwG9dofhr7m0yPXshl"
        "7s0-PXMum8c0-PXItm9s3-_TItWzC3-zzSq6uC_fNPj1yvTbvvX165Hpt3nv79Mj12rz39umR67V57-"
        "0T5FryLWv5Hw0v7s06ujoH5h1dnQPLjq6lA92qXv4ty_cu-YyLVuaSl2ALujoHLvmM4TyW-"
        "rFiprEMnhyvd3aFzbpGdnd2hc27RnZ3doUtukZ2d3aFLbtGdncuC3veNSuvd3aFXTorr3d2hV06K693doVdOiuvd3aFXTorr3cuCbvwG1QQ4M0"
        "-PXItm7E3-_TItWwa3-zTI9eyuX2zT49cyyb8zT6v5Oq6cN_s0yPXa_Pe26dHrtfmvbdPj1yvzXtvnx65Xpv33j5BriXfshZ0dQ7MOro6B-"
        "YdXZ0Di46upQPdql7-Lcv3LvmMi1bmkm9ZC7o6By75jGrNJEPdkwwXNInjXwv_6M_-l25fFi6c_j49cuU94vTy6ZGr6BGnl0-"
        "PXGWPOL18luTqnIQenV1hl9W7R2dX2GWl7dHZFXZZFXt0BmH_Ot1aCdm5NBcS7x3LujoHZh1dnQPzjq7OgUVHV-fAsqNLDVzwpy22RIv_bKW_"
        "T49ceY84vXx65Cp6xOnl0yNX2SNOL58luTonoUdnV9hl9e7R2RV2WWl7dHaFXVbFHp1BWH0vEXYuC-"
        "8lC7o6B2YdXZ0D846uzoFFR1fnwLKjSw1c8AOuLdHiH2f7-_"
        "TIlfWI08unR66iR5xePj1ylT3i9PJZkqtzEnp0doVdVu8enV1hl5W2R2dX2GVV7NEZhNX3EmHnsvBesqCrc2DW0dU5MO_"
        "o6hxYdHR1Diw7utTABT9T2BIt_gmiv0-PXFmPOL18euTKe8Tp5dMjV9kjTi-"
        "fJbk6J6FHZ1fYZfXu0dkVdllpe3R2hV1WxR6dQVh9LxF2LgvvJQu6OgdmHV2dA_OOrs6BRUdX58Cyo0sNXPAyzpZo8Yu2_"
        "j49cmU94vTy6ZEr7xGnl0-"
        "PXEWPOL18luTqnIQenV1hl9W7R2dX2GWl7dHZFXZZFXt0BmH1vUTYuSy8lyzo6hyYdXR1Dsw7ujoHFh1dnQPLjq56YP0_sILm9n_"
        "2JGxb7JovalvsWixqW-xaLmpTrlnb_LKobbFrvqhtsWuxqG2xa7moTbnmC841X3yu-"
        "YIpyBdPQb7gXPPF55ovONd88bkWC861WHyuxYJzLRafa7FgCorFU1AsONdi8bmWC861XHyu5YJzLRefa7ngXMvF51oumILSTUH9v4Qy3lOm6dh"
        "Xpu44_"
        "qRM1fHX6UahbdO1OdO27cq0bbq2drRtu2Jt264zbZuu7VTbtutK26Zr9KJt25Vp23TtJtq2XZfaNl3jd9q2XSNtm67JH23brkzbtivXtnTx5iY"
        "ro0XnsB9g6MCV0mLg0KwYxc6FK0exd8lCdi5cSYq9SxyydzkL2blwhSn2LlchO5fRS8jeJQvZuXAFKvYulyE7F65Ixd5lFLJz4QpV7F2ykL1LH"
        "rJyyeyqzdyqzeyqzdyqzeyqzdyqzYJVK-"
        "xc2lUr7F2ykJ1Lu2qFvUscsnc5C9m5tKtW2Ltchexc2lUr7F2ykJ1Lu2qFvctlyM6lXbXC3mUUsnNpV62wd8lC9i55yMolt6s2d6s2t6s2d6s2"
        "t6s2d6s2D1atsHNpV62wd8lCdi7tqhX2LnHI3uUsZOfSrlph73IVsnNpV62wd8lCdi7tqhX2LpchO5d21Qp7l1HIzqVdtcLeJQvZu-"
        "QhK5fCrtrCrdrCrtrCrdrCrtrCrdoiWLXCzqVdtcLeJQvZubSrVti7xCF7l7OQnUu7aoW9y1XIzqVdtcLeJQvZubSrVti7XIbsXNpVK-"
        "xdRiE7l3bVCnuXLGTvkoesXEq7aku3aku7aku3aku7aku3astg1Qo7l3bVCnuXLGTn0q5aYe8Sh-"
        "xdzkJ2Lu2qFfYuVyE7l3bVCnuXLGTn0q5aYe9yGbJzaVetsHcZhexc2lUr7F2ykL1LHjJdztsVe25W63m7Us_NKj1vV-"
        "i5WZ3namWKbbrqFSm27cq0bbrqFSi27Yq1bbvOtG266pUmtu260rbpqleW2LYr07bp2k20bbsutW266pUjtu0aadt01StFbNuVadt25dqWrr_"
        "MHl2hc9gPMHTgSmkxcGhWjGLnwpWj2LtkITsXriTF3iUO2buchexcuMIUe5erkJ3L6CVk75KF7Fy4AhV7l8uQnQtXpGLvMgrZuXCFKvYuWcjeJ"
        "Q9Zuag9ukLnsB9g6NCuWrtHlxa9aoM9etvUrtpgj66aspCdS7tqgz26aopD9i5nITuXdtUGe3TVdBWyc2lXbbBHV01ZyM6lXbXBHl01XYbsXNp"
        "VG-zRVdMoZOfSrtpgj66aspC9Sx6yclF7dIXOYT_"
        "A0KFdtXaPLi161QZ79LapXbXBHl01ZSE7l3bVBnt01RSH7F3OQnYu7aoN9uiq6Spk59Ku2mCPrpqykJ1Lu2qDPbpqugzZubSrNtijq6ZRyM6lX"
        "bXBHl01ZSF7lzxk5aL26Aqdw36AoUO7au0eXVr0qg326G1Tu2qDPbpqykJ2Lu2qDfboqikO2buchexc2lUb7NFV01XIzqVdtcEeXTVlITuXdtU"
        "Ge3TVdBmyc2lXbbBHV02jkJ1Lu2qDPbpqykL2LnnIykXt0RU6h_0AQ4d21do9urToVRvs0dumdtUGe3TVlIXsXNpVG-"
        "zRVVMcsnc5C9m5tKs22KOrpquQnUu7aoM9umrKQnYu7aoN9uiq6TJk59Ku2mCPrppGITuXdtUGe3TVlIXsXfKQaxf8fw3WK1ZM07GvTN2BFSqm"
        "6uDKbGzThRXZ2LYr07bpwgpsbNsVa9t2nWnbdGGlNbbtutK26Rq9aNt2Zdo2XVhJjW27LrVturByGtt2jbRturBSGtt2Zdq2Xbm2pesvvUfX6B"
        "z2AwwduFJaDByaFaPYuXDlKPYuWcjOhStJsXeJQ_"
        "YuZyE7F64wxd7lKmTnMnoJ2btkITsXrkDF3uUyZOfCFanYu4xCdi5coYq9Sxayd8lDVi7tHl2jc9gPMHRoV63ZozctetXaPbpqalet3aPrpixk"
        "59KuWrtH101xyN7lLGTn0q5au0fXTVchO5d21do9um7KQnYu7aq1e3TddBmyc2lXrd2j66ZRyM6lXbV2j66bspC9Sx6ycmn36Bqdw36AoUO7as"
        "0evWnRq9bu0VVTu2rtHl03ZSE7l3bV2j26bopD9i5nITuXdtXaPbpuugrZubSr1u7RdVMWsnNpV63do-"
        "umy5CdS7tq7R5dN41Cdi7tqrV7dN2Uhexd8pCVS7tH1-gc9gMMHdpVa_"
        "boTYtetXaPrpraVWv36LopC9m5tKvW7tF1UxyydzkL2bm0q9bu0XXTVcjOpV21do-um7KQnUu7au0eXTddhuxc2lVr9-"
        "i6aRSyc2lXrd2j66YsZO-"
        "Sh6xc2j26RuewH2Do0K5as0dvWvSqtXt01dSuWrtH101ZyM6lXbV2j66b4pC9y1nIzqVdtXaPrpuuQnYu7aq1e3TdlIXsXNpVa_"
        "fouukyZOfSrlq7R9dNo5CdS7tq7R5dN2Uhe5c85MrlH0dPf3HvFL2zFHYPLLXdmRmdhaMzMzoLR-dmdB6Ozs3oPBxdmNFFOLowo4twdGlGl-"
        "Ho0owuzeh25P_i_"
        "7mpHdgOkp5BPRnbHzioNU3HQJnSoSbHonMYBKgcMhshcxEyGyFzEXIbIXcRchshdxEKG6FwEQoboXARShuhdBFKG6E0EVba0Stm5Eo7asWOsLV"
        "X6BwGASqHzEbIXITMRshchNxGyF2E3EbIXYTCRihchMJGKFyE0kYoXYTSRrC1_9KO_"
        "mJGfmlHfbEjbO0VOodBgMohsxEyFyGzETIXIbcRchchtxFyF6GwEQoXobARChehtBFKF6G0EWzt120lFTqHQYDKIbMRMhchsxEyFyG3EXIXIbc"
        "RchehsBEKF6GwEQoXobQRShehtBGCSqrR5p7f9A1Mn4zbsDOg0DkMAlQOmY2QuQiZjZC5CLmNkLsIuY2QuwiFjVC4CIWNULgIpY1QugiljWBnY"
        "EONDmdgQw0MZmB43YwT03QMlCkdZs40OodBgMohsxEyFyGzETIXIbcRchchtxFyF6GwEQoXobARChehtBFKF6G0EcycDf-0o_-YkX_aUX_"
        "MiGk7YmpGTNsRUzvCztbUzdbUztbUzdbUztbUzdbUztbUzdbUztbUzdbUztbUzdbUztbUzdbUztbUzdbUztbUzdbUztbUzdZNO_"
        "rGjLxpR93YEbb2N672N7b2N672N7b2N672N7b2N672N7b2N672N7b2N672N7b2N672N7b2N672N7b2N672N7b2N672s3b0zIyctaNmdoSt_"
        "czVfmZrP3O1n9naz1ztZ7b2M1f7ma39zNV-Zms_c7Wf2drPXO1ntvYzV_uZrf3M1X5maz9ztZ-3o-dm5LwdNbcjbO3nrvZzW_u5q_3c1n7uaj-"
        "3tZ-72s9t7eeu9nNb-7mr_dzWfu5qP7e1n7vaz23t5672c1v7eVj70ftmtJimY6BM6TC11-"
        "gcBgEqh8xGyFyEzEbIXITcRshdhNxGyF2EwkYoXITCRihchNJGKF2E0kawtR-0owdm5KAdNbAjbO0HrvYDW_uBq_3A1n7gaj-wtR-"
        "42g9s7Qeu9gNb-4Gr_"
        "cDWfuBqP7C1H7jaD2ztB672A1v7gav9ajt61YxcbUet2hG29quu9qu29quu9qu29quu9qu29quu9qu29quu9qu29quu9qu29quu9qu29quu9qu"
        "29quu9qu29quu9mvt6DUzcq0dtWZH2Nqvudqv2dqvudqv2dqvudqv2dqvudqv2dqvudqv2dqvudqv2dqvudqv2dqvudqv2dqvudqv2dqvhbXfu"
        "zeV1OgcBgEqh8xGyFyEzEbIXITcRshdhNxGyF2EwkYoXITCRihchNJGKF2E0kYIKqlGB_"
        "th9g1MH8dFH5txYpqOgTKlw8yZRucwCFA5ZDZC5iJkNkLmIuQ2Qu4i5DZC7iIUNkLhIhQ2QuEilDZC6SKUNoKZs6i960fmrh-1d_"
        "1o1Y6wtXd3_cje9SN314_sXT9yd_3I3vUjd9eP7F0_cnf9yN71I3fXj-xdP3J3_cje9SN314_sXT9yd_3I3vUjd9eP2rt-"
        "ZO76UXvXj9bsCFt7d9eP7F0_cnf9yN71I3fXj-xdP3J3_cje9SN314_sXT9yd_3I3vUjd9eP7F0_cnf9yN71I3fXj-xdP3J3_"
        "ah9eyqm6RgoUzps7d2b68i-uY7cm-vIvrmO3JvryL65jtyb68i-uY7cm-vIvrmO3JvryL65jtyb68i-uY7cm-"
        "vIvrmO3JvryL65jtyb62izHb1pRm62ozbtCFv7TVf7TVv7TVf7TVv7TVf7TVv7TVf7TVv7TVf7TVv7TVf7TVv7TVf7TVv7TVf7TVv7TVf7TVv7"
        "TVf7rXb0lhm51Y7asiNs7bdc7bds7bdc7bds7bdc7bds7bdc7bds7bdc7bds7bdc7bds7bdc7bds7bdc7bds7bdc7bds7bfC2o9_"
        "NaPFNB0DZUqHqb1G5zAIUDlkNkLmImQ2QuYi5DZC7iLkNkLuIhQ2QuEiFDZC4SKUNkLpIpQ2gq39XTv6zoy8a0fd2RG29neu9ne29neu9ne29n"
        "eu9ne29neu9ne29neu9ne29neu9ne29neu9ne29neu9ne29neu9ne29neu9u2-"
        "QEzTMVCmdNjauz2ZahkEqBwyGyFzETIbIXMRchshdxFyGyF3EQoboXARChuhcBFKG6F0EUobwdb-oR39YEY-tKMe7Ahb-wdX-wdb-wdX-wdb-"
        "wdX-wdb-wdX-wdb-wdX-wdb-wdX-wdb-wdX-wdb-wdX-wdb-wdX-wdb-"
        "4ew9vGkGS2m6RgoUzpM7TU6h0GAyiGzETIXIbMRMhchtxFyFyG3EXIXobARChehsBEKF6G0EUoXobQRbO3be05s7jlxe8-J7-"
        "0IW3t3z4ntPSd295zY3nNid8-J7T0ndvec2N5zYnfPie09J3b3nNjec2J3z4ntPSd295zY3nNid8-J7T0ndvecuP31Kja_XsXtr1fx3I6wtXe_"
        "XqmWQYDKIbMRMhchsxEyFyG3EXIXIbcRchehsBEKF6GwEQoXobQRShehtBFs7R_b0Y9m5GM76tGOsLV_dLV_tLV_dLV_tLV_dLV_tLV_dLV_"
        "tLV_dLV_tLV_dLV_tLV_dLV_tLV_dLV_tLV_dLV_tLV_dLV_akc_mZFP7agnO8LW_snV_snW_snV_snW_snV_snW_snV_snW_snV_snW_snV_"
        "snW_snV_snW_snV_snW_snV_snW_snVXtXAfn712e3nVp_"
        "Zfl71We3nzO1s5W62cjtbuZut3M5W7mYrt7OVu9nK7WzlbrZyO1u5m63czlbuZiu3s5W72crtbOVutnI7W7mbLZXf5lZ5bc7C1r5wtS9s7QtX-"
        "8LWvnC1L2ztC1f7wta-cLUvbO0LV_vC1r5wtS9s7QtX-8LWvnC1L2zti7D2k_YJMTFPiEn7hJg82hGm9hP3hJjYJ8TEPSEm9gkxcU-"
        "IiX1CTNwTYmKfEBP3hJjYJ8TEPSEm9gkxcU-IiX1CTNwTYmKfEBP3hJjYJ8TEPSEm7RNiYp4Qk_YJMXmyI2zt3RNiYp8QE_"
        "eEmNgnxMQ9ISb2CTFxT4iJfUJM3BNiYp8QE_eEmNgnxMQ9ISb2CTFxT4iJfUJM3BNiYp8QE_"
        "eESNq3zmKajoEypcPUXqNzGASoHDIbIXMRMhshcxFyGyF3EXIbIXcRChuhcBEKG6FwEUoboXQRShvB1r596yym6RgoUzps7d1bZ9UyCFA5ZDZC"
        "5iJkNkLmIuQ2Qu4i5DZC7iIUNkLhIhQ2QuEilDZC6SKUNoKt_"
        "XY72vwkTxooUzps7bdd7bdt7bdd7bdt7bdd7bdt7bdd7bdt7bdd7bdt7bdd7bdt7bdd7bdt7bdd7bdt7bdd7bdt7bdd7Xfb0btm5G47ateOsLX"
        "fdbXftbXfdbXftbXfdbXftbXfdbXftbXfdbXftbXfdbXftbXfdbXftbXfdbXftbXfdbXftbXfdbXfb0fvm5H77ah9O8LWft_Vft_Wft_Vft_"
        "Wft_Vft_Wft_Vft_Wft_Vft_Wft_Vft_Wft_Vft_Wft_Vft_Wft_Vft_Wft_V_qAdfWBGHrSjDuwIW_sDV_sDW_sDV_sDW_sDV_sDW_sDV_"
        "sDW_sDV_sDW_sDV_sDW_sDV_sDW_sDV_sDW_sDV_sDW_sDV_"
        "vDdvShGXnYjjq0I2ztD13tD23tD13tD23tD13tD23tD13tD23tD13tD23tD13tD23tD13tD23tD13tD23tD13tD23tD13to3Z0ZEZG7ajIjrC1"
        "j1ztI1v7yNU-srWPXO0jW_vI1T6ytY9c7SNb-8jVPrK1j1ztI1v7yNU-srWPXO0jW_vI1X7cjh6bkeN21NiOsLUfu9qPbe3HrvZjW_uxq_"
        "3Y1n7saj-2tR-72o9t7ceu9mNb-7Gr_djWfuxqP7a1H7vaj23tx672R-3oIzPyqB11ZEfY2h-52h_Z2h-52h_Z2h-52h_Z2h-52h_Z2h-52h_"
        "Z2h-52h_Z2h-52h_Z2h-52h_Z2h-52h_Z2h-52h-3o4_NyON21LEdYWt_7Gp_bGt_7Gp_bGt_7Gp_bGt_7Gp_bGt_7Gp_bGt_7Gp_bGt_7Gp_"
        "bGt_7Gp_bGt_7Gp_bGt_7Gp_0o4-MSNP2lEndoSt_Ymr_Ymt_Ymr_Ymt_Ymr_Ymt_Ymr_Ymt_Ymr_Ymt_Ymr_Ymt_Ymr_Ymt_Ymr_Ymt_Ymr_"
        "Ymt_Ymr_bd29Dcz8ls76psZcdqOODUjTtsRp3qE_A9uTrVtu260bbtutW27ZtqWLs50k0-xd7kJ2bvchuxdZiErlyw4l8yfSxacS-"
        "bPJQvOJfPnkgXnkvlzyYNzyf255MG55P5c8uBccn8ueXAuuT-XIjiXwp9LEZxL4c-lCM6l8OdSBOdS-HMpg3Mp_bmUwbmU_"
        "lzK4FxKfy5lcC6lPZdzdR7n9hzOVf5zm_tc5T23Oc9VvvMgV3CdaPYuNyF7l9uQvcssZOWSBeeS-XPJgnPJ_Llkwblk_"
        "lyy4Fwyfy55cC65P5c8OJfcn0senEvuzyUPziX351IE51L4cymCcyn8uRTBuRT-XIrgXAp_LmVwLqU_lzI4l9KfSxmcS-nPpQzOxV4n8h-"
        "enmrbdt1o23bdatt2zbQtXfY6MexdbkL2Lrche5dZyMolC84l8-eSBeeS-XPJgnPJ_Llkwblk_"
        "lzy4Fxyfy55cC65P5c8OJfcn0senEvuz6UIzqXw51IE51L4cymCcyn8uRTBuRT-XMrgXEp_LmVwLqU_lzI4l9KfSxmci75O_nESg3Z_V-b_"
        "4v9Z0JP2PH_SfSHVabfx36oYJzDrzr9J9f11uvFJdS5AOuPPxA-_K7Pu-PqpossjZbJjr_50z9pm16QdMzFj8KX_ZEWZvM28R4AD_G9MvJeTO-"
        "B_vvLwGjg60za77mtMtc2uh_oTpto2d5itFW1XXf8_vFUDyA");
    static string all_emojis_str = gzdecode(base64url_decode(packed_emojis).ok()).as_slice().str();
    constexpr size_t EMOJI_COUNT = 7539;
#else
    string all_emojis_str;
    constexpr size_t EMOJI_COUNT = 0;
#endif
    FlatHashSet<Slice, SliceHash> all_emojis;
    all_emojis.reserve(EMOJI_COUNT);
    for (size_t i = 0; i < all_emojis_str.size(); i++) {
      CHECK(all_emojis_str[i] != ' ');
      CHECK(all_emojis_str[i + 1] != ' ');
      size_t j = i + 2;
      while (j < all_emojis_str.size() && all_emojis_str[j] != ' ') {
        j++;
      }
      CHECK(j < all_emojis_str.size());
      all_emojis.insert(Slice(&all_emojis_str[i], &all_emojis_str[j]));
      CHECK(j - i <= max_emoji_length);
      i = j;
    }
    CHECK(all_emojis.size() == EMOJI_COUNT);
    return all_emojis;
  }();
  if (str.size() > MAX_EMOJI_LENGTH) {
    return false;
  }
  return emojis.count(str) != 0;
}

int get_fitzpatrick_modifier(Slice emoji) {
  if (emoji.size() < 4 || emoji[emoji.size() - 4] != '\xF0' || emoji[emoji.size() - 3] != '\x9F' ||
      emoji[emoji.size() - 2] != '\x8F') {
    return 0;
  }
  auto c = static_cast<unsigned char>(emoji.back());
  if (c < 0xBB || c > 0xBF) {
    return 0;
  }
  return (c - 0xBB) + 2;
}

Slice remove_fitzpatrick_modifier(Slice emoji) {
  while (get_fitzpatrick_modifier(emoji) != 0) {
    emoji.remove_suffix(4);
  }
  return emoji;
}

string remove_emoji_modifiers(Slice emoji, bool remove_selectors) {
  string result = emoji.str();
  remove_emoji_modifiers_in_place(result, remove_selectors);
  return result;
}

void remove_emoji_modifiers_in_place(string &emoji, bool remove_selectors) {
  static const Slice modifiers[] = {u8"\uFE0F" /* variation selector-16 */,
                                    u8"\u200D\u2640" /* zero width joiner + female sign */,
                                    u8"\u200D\u2642" /* zero width joiner + male sign */,
                                    u8"\U0001F3FB" /* emoji modifier fitzpatrick type-1-2 */,
                                    u8"\U0001F3FC" /* emoji modifier fitzpatrick type-3 */,
                                    u8"\U0001F3FD" /* emoji modifier fitzpatrick type-4 */,
                                    u8"\U0001F3FE" /* emoji modifier fitzpatrick type-5 */,
                                    u8"\U0001F3FF" /* emoji modifier fitzpatrick type-6 */};
  const size_t start_index = remove_selectors ? 0 : 1;
  size_t j = 0;
  for (size_t i = 0; i < emoji.size();) {
    bool is_found = false;
    for (size_t k = start_index; k < sizeof(modifiers) / sizeof(*modifiers); k++) {
      auto length = modifiers[k].size();
      if (i + length <= emoji.size() && Slice(&emoji[i], length) == modifiers[k]) {
        // skip the modifier
        i += length;
        is_found = true;
        break;
      }
    }
    if (!is_found) {
      emoji[j++] = emoji[i++];
    }
  }
  if (j != 0) {
    emoji.resize(j);
  }
}

string remove_emoji_selectors(Slice emoji) {
  if (!is_emoji(emoji)) {
    return emoji.str();
  }
  string str;
  for (size_t i = 0; i < emoji.size(); i++) {
    if (i + 3 <= emoji.size() && emoji[i] == '\xEF' && emoji[i + 1] == '\xB8' && emoji[i + 2] == '\x8F') {
      // skip \uFE0F
      i += 2;
    } else {
      str += emoji[i];
    }
  }
  CHECK(is_emoji(str));
  return str;
}

}  // namespace td
