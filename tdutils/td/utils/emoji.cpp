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
  constexpr size_t MAX_EMOJI_LENGTH = 35;
  static const FlatHashSet<Slice, SliceHash> emojis = [max_emoji_length = MAX_EMOJI_LENGTH] {
#if TD_HAVE_ZLIB
    Slice packed_emojis(
        "eJyNndlS40q3rV-lIvbduTp983b0UGAMNriRTVONsDCmcUNnUBOxH6X2A5wVcV7gSENjYM20pPov9JGfcqY0czotyaZY609j8O1PY_B_30_"
        "SH8NsQ_NknG158y7b8uYk2_LmfbblzWm25c1FtqHZi7Itb8bZhmb_PNvyZifb0PQOsi1vfs-"
        "2vHmYbXnzKNvyZiPb8uZxtuXNZrblTWx58zTb8mYr2_"
        "JmO9vy5lm25c0k29AcnGVb3vyZbXnzLtvy5iTb8maUbXkzzjY0hzvZljd3sy1vNrMtb55nW968y7a8Oc-"
        "2vLnItrz5km158yPb8maUbWhe7GZb3jzMtrx5lG158zbb0LxsZFvebGZb3jzLtrx5nm15s5NtebOXbWhedbItb3azLW_"
        "2si1vTrMtbybZhub9MNvy5kW2oflwmm15s5NtWfOf640dIJftHLnsNQFKC6C0AcoZQDkHKB2A0gUoPYDSBygeQBkAuRxsApQBQHkCKHOAsgAoz"
        "wDlBaC8ApR3gLIEKB9ALt9PAUoLyKWxAVA2AcoWQNkGKDsAZReg7AGUfYByAFC-"
        "A5RDgHIEUBoA5RigNAFKDsopQGkBlDZAOQMo5wClA1C6AKUHUPoAxQMoA4AyBCgXAOUSoFwBlGuA8gOgPACUR4DyBFCmAGUGUOYAZQFQngHKC0"
        "B5AyjvAGUJUD4AyidACQFKBFBigJIAuRxvAJRNgLIFULYByg5A2QUoewBlH6AcAJTvAOUQoBwBlAZAOQYoTYCSg3IKUFoApQ1QzgDKOUDpAJQu"
        "QOkBlD5A8QDKAKAMAcoFQLkEKFcA5Rqg_"
        "AAoPwHKL4DyG6D4AOUGoIwASgBQbgHKGKDcAZQJQLkHKA8A5RGgPAGUKUCZAZQ5QFkAlGeA8gJQXgHKG0B5ByhLgPIBUD4BSghQYoCSALk0NwD"
        "KJkDZAijbAGUHoOwClD2Asg9QDgDKd4ByCFCOAEoDoBwDlCZAyUE5BSgtgNIGKGcA5QdA-"
        "QlQfgGU3wDFByg3AGUEUAKAcgtQxgDlDqBMAMo9QHkAKI8A5QmgTAHKDKDMAcoCoDwDlBeA8gpQ3gDKO0BZApQPgPIJUEKAEgGUGKAkQC4nGwB"
        "lE6BsAZRtgLIDUHYByh5A2QcoBwDlO0A5BCg5KKcApQVQ2gDlDKD8ACg_"
        "AcovgPIboPgA5QagjABKAFBuAcoYoNwBlAlAuQcoDwDlEaA8AZQpQHkGKO8AZQlQPgDKJ0AJAUoEUGKAkgC5nG4AlE2AsgVQtgHKDkDZBSh7AG"
        "UfoBwAlO8A5RCgHAGUBkA5BihNgJKDcgpQWgClDVDOAMo5QOkAlC5A6QGUPkDxAMoAoAwBygVAuQQoVwDlGqD8ACg_"
        "AcovgPIboPgA5QagjABKAFBuAcoYoNwBlAlAuQcoDwDlEaA8AZQpQJkBlDlAWQCUZ4DyAlBeAcobQHkHKEuA8gFQPgFKCFAigBIDubQ2AMoWQN"
        "kGKDsAZReg7AGUfYByAFC-A5RDgHIEUBoA5RigNAFKDsopQGkBlDZAOQMo5wClA1C6AKUHUPoAxQMoA4AyBCgXAOUSoFwBlGuA8gOg_"
        "AQovwDKb4DiA5QbgDICKAFAuQUoY4ByB1AmAOUeoDwAlEeA8gRQpgBlBlDmAGUBUJ4BygtAeQUobwDlHaAsAcoHQPkEKCFAiQBKDFASIJf2BkD"
        "ZBChbAGUboOwAlF2AsgdQ9gHKAUD5DlAOAcoRQGkAlGOA0gQoOSinAKUFUNoA5QygnAOUDkDpApQeQOkDFA-gDADKEKBcAJRLgHIFUK4Byg-"
        "A8hOg_"
        "AIovwGKD1BuAMoIoAQA5RagjAHKHUCZAJR7gPIAUB4ByhNAmQKUGUCZA5QFQHkGKC8A5RWgvAGUd4CyBCgfAOUToIQAJQIoMUBJgFzONgDKJkD"
        "ZAijbAGUHoOwClD2Asg9QDgDKd4ByCFCOAEoDoBwDlCZAyUE5BSgtgNIGKGcA5RygdABKF6D0AEofoHgAZQBQhgDlAqBcApQrgHINUH4AlJ8A5"
        "RdA-Q1QfIByA1BGACUAKLcAZQxQ7gDKBKDcA5QHgPIIUJ4AyhSgzADKHKAsAMozQHkBKK8A5Q2gvAOUJUD5ACifACUEKAmQy_"
        "kGQNkEKFsAZRug7ACUXYCyB1D2AcoBQPkOUA4ByhFAaQCUY4DSBCg5KKcApQVQ2gDlDKCcA5QOQOkClB5A6QMUD6AMAMoQoFwAlEuAcgVQrgHK"
        "D4DyE6D8Aii_"
        "AYoPUG4AygigBADlFqCMAcodQJkAlHuA8gBQHgHKE0CZApQZQJkDlAVAeQYoLwDlFaC8AZR3gLIEKB8A5ROghAAlAnLpHAGUBkA5BihNgHIKUF"
        "oApQ1QzgDKOUDpAJQuQOkBlD5A8QDKAKAMAcoFQLkEKFcA5Rqg_"
        "AAoPwHKL4DyG6D4AOUGoIwASgBQPoBcuh2A0gUoPpBL7xOghAAlAigxQEmAXPobAGUToGwBlG2AsgNQdgHKHkDZBygHAOU7QDkEKEcApQFQjgF"
        "KE6DkoJwClBZAaQOUM4ByDlA6AKULUHoApQ9QPIAyAChDgHIBUC4ByhVAuQYoPwDKT4DyC6D8Big-"
        "QLkBKCOAEgCUW4AyBih3AGUCUO4BygNAeQQoTwBlClBmAGUOUBYA5RmgvACUV4DyBlDeAcoSoHwAlE-"
        "AEgKUCKDEACUBcvE2AMomQNkCKNsAZQeg7AKUPYCyD1AOAMp3gHIIUI4ASgOgHAOUJkDJkctgA6BsApQtgLINUHYAyi5A2QMo-"
        "wDlAKB8ByiHAOUIoDQAyjFAaQKUHJRTgNICKG2AcgZQzgFKB6B0AUoPoPQBigdQBgBlCFAuAMolQLkCKNcA5QdA-"
        "QlQfgGU3wDFByg3AGUEUAKAcgtQxgDlDqBMAMo9QHkAKI8A5QmgTAHKDKDMAcoCoDwDlBeA8gpQ3gDKO0BZApQPgPIJUEKAEgGUGKAkQC7DDYC"
        "yCVC2AMo2QNkBKLsApQFQTgFKC6C0AUoHoHQBSg-gXACUS4ByBVCuAcoEoNwDlGeA8gJQXgHKG0B5ByhLgPIBUD4BSgjkcv0DoPwEKL8Aym-"
        "A4gOUG4AyAigBQLkFKGOAcgdQJgBlCuTiNwDKMUBpApQclFOA0gIobYByBlDOAUoHoHQBSg-g9AGKB1AGAGUIUC4AyiVAuQIo1wDlB0D5CVB-"
        "AZTfAMUHKDcAZQRQAoByC1DGAOUOoEwAyj1AeQAojwDlCaBMAcoMoMwBygKgPAOUF4DyClDeAMo7QFkClA-"
        "AEgKUCKDEACUBcrnZACibAGULoGwDlB2AsgtQ9gHKAUD5DlAOAcoRQGkAlGOA0gQoOSinAKUFUNoA5QygnAOUDkDpApQeQOkDFA-"
        "gDADKEKBcAJRLgHIFUK4Byg-A8hOg_"
        "AIovwGKD1BuAMoIoAQA5RagjAHKHUCZAJR7gPIAUB4ByhNAmQKUGUCZA5QFQHkGKC8A5RWgvAGUd4CyBCgfAOUToIQAJQIoMUBJgFxGGwBlE6B"
        "sAZRtgLIDUHYByh5A2QcoBwDlO0A5BChHAKUBUI4BShOg5KCcApQWQGkDlDOAcg5QOgClC1B6AKUPUDyAMgAoQ4ByAVAuAcoVQLkGKD8Ayk-"
        "A8gug_"
        "AYoPkC5ASgjgBIAlFuAMgYodwBlAlDuAcoDQHkEKE8AZQpQZgBlDlAWAOUZoLwAlFeA8gZQ3gHKEqB8AJRPgBIClAigxAAlAXIJNgDKJkDZAij"
        "bAGUHoOwClD2Asg9QDgDKd4ByCFCOAEoDoBwDlCZAyUE5BSgtgNIGKGcA5RygdABKF6D0AEofoHgAZQBQhgDlAqBcApQrgHINUH4AlJ8A5RdA-"
        "Q1QfIByA1BGACUAKLcAZQxQ7gDKBKDcA5QHgPIIUJ4AyhSgzADKHKAsAMozQHkBKK8A5Q2gvAOUJUD5ACifACUEKBFAiQFKAuQyngKUGUCZA5Q"
        "FQHkGKC8A5RWgvAGUd4CyBCgfAOUToIRALncbAGUToGwBlG2AsgNQdgHKHkDZBygHAOUUoLQAShugnAGUc4DSAShdgNIDKH2A4gGUAUAZApQLg"
        "HIJUK4AyjVA-"
        "QFQfgKUXwDlN0DxAcoNQBkBlACg3AKUMUC5AygTgHIPUB4AyiNAeQIoU4AyAyhzgLIAKM8A5QWgvAKUN4DyDlCWAOUDoHwClBCgRAAlAXKZbAC"
        "UTYCyBVC2AcoOQNkFKE2AkoNyClBaAKUNUM4AyjlA6QCULkDpAZQ-QPEAygCgDAHKD4DyE6D8Aii_"
        "AYoPUG4AygigBADlFqBMAcoMoMwBygKgPAOUF4DyClDeAMo7kMm_j7_9--O3Pxvhtz-b37_92fmVbstvf_"
        "bO062Tbt1066VbP928dBun2923P43bb39Osr-rfUi3dPzJU7rN0m2ebu_plh7j5OPbn7Otb3-6aXx3km6v3_70NtLtM93S8_XTdn8z3dKY_"
        "na67aRbM91a6Zaer3-Zbj_SLc2p_zvdRumWHqufnq-fnq-fnqefnqefnsdLj-Wlx_Gu0y0d46Xx3k26pWO8NFcvPaeX_"
        "RlsO93SuQ3SuQ3SuQ3SuQ3SeQ2G6XaRbunYQZBu03RL5zM8SLfsj1rTnIZn6ZbOf5j2DbO-53R7S7c0j2Gax0V6_os0_iKt40Uj3Y7TLfsj1_"
        "ScF-k5L9LzXaRzuviZbot0S8dfpnO-3E-3NN9L_9ufq7TPT_f7L9_-3O-m21667X_7j41pukXf_uOwl27ZCt7NlsputkR2sxvpbvb-"
        "PcjenQf4a7lswTay5djIlmEjW36NbNk1suXWyK5-jeyq18iudo3sKtfAXwzhjyiy91Eze_"
        "80sxM1s7dIM3trNLNreDO7dp9kj5Qn2aPkSfYIeZK9w0-yN-ZJ9oY8yQ5wkh3gJHvjnWRHOcmOcpId5SS7R5xk94aT7HgnOF62oE-"
        "ytXuCf7KcTaaFf9SVJdTJHm072SNtJ7vQdrKZd7IRnWzpd7JhnSz7Tja2ky3tTnY57Ga30G42rJul283S7WbpdrPLTjerSzerRjcrYjd7f3Xxu"
        "-WsiL3s0tbLLmm97LLUyy5Hvewy1Muy72XZ97Lse1mde9k1oJcdqpfl18tS6-Hb3ey8w-yUw6xCw-"
        "ytN8yuK8Ns2DC7jgyzscPshRpmCQ2zV2aYTXC4-PZvf7Z_f_u39E2a_fxPAOU_A5T_AlD-K0D5bwDlvwOU_wFQ_idA-V8A5X8DlP8DUP653h-"
        "l260aYzXu1Jioca_"
        "GoxozNeZqPKvxqsabGu9qLNX4UCNUI1IjWzj7QSZqBGqM1bhTY6LGvRoPajyq8aTGTI25Ggs1ntV4VeNNjXc1lmp8qhGqEauB5G-V_"
        "K3Ke6vkb5XzrXK-Vc63yjlrTNWYqTFXY6HGsxovaryp8aHGpxqhGpEasRrIeayqjpXYWKUbK5-"
        "x0hjr7GMNv9OU7zTlOx3wTge800zvlOqdynun8t4p-YmqMVEaE6UxURoTpTHRAe-Vxr2Wzb0qf6987vUS3CuxeyV2r5Peq_L3Ote9Kn-"
        "vgt9rtdwXz_6uxlKNDzVCNVD5B03nQad40CkedMAHHedBx3lUeR81r0fN61E5P-"
        "qAjzrgowr1qJwfdYpH5fyocz3pgE86zpOGP2nuU8VMVcOpajhVDacaPlUa0-"
        "LwNzVCNWI1sKJmeilneilnmvtMp5iphjMdcKbpzDSdmUqXNT7VwLnmOsVcR56rqnNNcK7VMtdM55rpXGefq_JzTXmuKc9VurnmPtdLMFfOc-"
        "U8V85z5TxXznMVKmtEanzNAhVbaDoLTWehWSw0i4VmsVANF0p-"
        "oVQXSnWhDBfKZ6FzPWumLzrpi871onO96FwvqtiLKvaik74Uj7NQ402NdzWWaoRqYO6vOvubzv6mWbxp-"
        "JuSf9Pwd43KGoEat2qM1bhT416NBzUe1XhSY6rGTI25Ggs1ntV4U-"
        "NdjaUan2pEasRqoPJLJb9UzkvlvFTll8p5qZyXSnWpVJdKdalUl0p1qVSXSnWpDJfKcKliLpXYhxL70Nk_dOQPHflDU_7QvD40_"
        "FPDPzWvT70Enzrgpyr_qQN-6sUNNfdQp4g001jHiTWLROdKlGHC6Zw8_78fm0G6_Uq3m3R7pCdu5yLdfqfbc1nnW7rdMyjJPgXhj1rznyF_"
        "RvwZ82eSfQLJ4_Az5M-IP2P-TLL_"
        "CE8eh58hf0b8GfNnFnfEuCPGHTHuiHFHjGswrsG4BuMajGsw7phxx4w7Ztwx4475h7jNXf4VLxqhGpEasRqo3Jb-5HdLf-67pT_13dKf-"
        "W4peFvB2wreVvC2grcVvKPgHQXvKHhHwTsK3lfwvoL3Fbyv4H0FHyr4UMGHCj5U8KGCjxR8pOAjBR8p-"
        "EjBDQU3FNxQcEPBDQa3VLqWStdS6VoqXUula6l0LZWupdK1VLqWStfaU_"
        "CegvcUvKfgPQWrdC2VrqXStVS6lkrXOlDwgYIPFHyg4AMFf1fwdwV_V_"
        "B3BX9XsF6Ull6Ull6Ull6Ull6Ull6Ull6Ull6Ull6Ull6Ull6Ull6Ull6Ull6U1teLcqzgYwUfK_"
        "hYwXqntJoKbiq4qeCmgpsKPlHwiYJPFHyi4BMFnyr4VMGnCj5V8KmCRwoeKXik4JGCRwoOFBwoOFBwoOBAwbcKvlXwrYJvFXyr4LGCxwoeK3is"
        "4LGCJwqeKHii4ImCJwq-V_C9gu8VfK_gewU_"
        "KPhBwQ8KflDwg4IfFfyo4EcFPyr4UcFTBU8VPFXwVMFTBc8UPFPwTMEzBc8UPFfwXMFzBc8VPFfwQsELBS8UvFDwQsHPCn5W8LOCnxX8rOAXBb"
        "8o-EXBLwp-UfCrgl8V_KrgVwW_KvhNwW8KflPwm4LfFPyu4HcFvyv4XcHvCg4VHCo4VHCo4JDB7U0GoxGqEakRq4FgXZ_buj63dX1u6_"
        "rc1vW5retzW9fntq7PbV2f27o-t3WHbesO29Ydtq07bFt32LYu5m1dzNu6mLd1MW_rYt7Wxbyti3lbF_"
        "O2LuZtXczbuiK1dUVq64rU1hWprStSu6XgloJbCm4puKXgOwXfKfhOwXcKvmNwR0u0oyXa0RLtaIl2tEQ7WqIdLdGOlmhHS7SjJdr5UPCHgj8U"
        "_KHgDwZ3TxmMRqhGpEasBoI7Cu4ouKPgjoI7Cu4quKvgroK7Cu4y2NPa8LQ2PK0NT2vD09rwtDY8rQ1Pa8PT2vC0NjytDU9rw9Pa8LQ2PK0NT3"
        "dYT3dYT3dYT3dYT3dYT3dYT3dYT3dYT3dYT3dYT3dYT3dYT3dYT3dYT3dYT3dYT3dYT3dYT3dYT3dYT-"
        "vZ03r2tJ49rWdP63nwm8FohGpEasRqIFhLdKAlOtASHWiJDrREB1qiAy3RgZboQEt0oCU60FV0oKvoQFfRga6iA11FhxsMRiNUI1IjVgPBelGG"
        "elGGelGGelGGelF8BfsK9hXsK9j_CladfdXZV5191dlXnf2-gvsK7iu4r-C-gj0Fewr2FOwp2FPwQMEDBQ8UPFDwQMFDBQ8VPFTwUMFDBV8o-"
        "ELBFwq-UPCFgi8VfKngSwVfKvhSwVcKvlLwlYKvFHyl4GsFXyv4WsHXCr5WsB4CfT0E-noI9PUQ6Osh0J8qeKrgqYKnCp4qWE8yvp5kfD3J-"
        "HqS8fUk4-"
        "tJxteTjK8nGV9PMr6eZHw9yfh6kvH1JOPrScbXk4yv96Cv96Cv96Cv96Cv96Cv96Cv96Cv96Cv96Cv96Cv96Cv96Cv96Cv96Cv96CvJxlfTzK-"
        "nmR8Pcn4epLx9STj60nG15OMrycZX08y_"
        "lLBSwUvFbxU8FLBkYIjBUcKjhQcKThWcKzgWMGxgmMG32iCN5rgjSZ4owneaIIj1XmkOo9U55HqPFKdR6rzSHUeqc4j1XmkOo9UupFKN1LpRir"
        "dSKUbqXQjlW6k0o1UupFKN_pU8KeCPxX8qeBPBge6WwW6WwW6WwW6WwW6WwW6WwW6WwW6WwW6WwW6WwW6iga6iga6iga6iga6igZ6-"
        "gr09BXo6SvQ01egp6-greC2gtsKbiu4reAzBZ8p-EzBZwo-U_"
        "C5gs8VfK7gcwWfK1jPSIGekQI9IwV6Rgr0jBToGSnQM1KgZ6RAz0iBnpGCnoJ7Cu4puKfgnoJ1Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0"
        "Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0Awp0A5roc8pEn1Mm-"
        "pwy0eeUiT6nTPQV3ERfwU30FdxEX8FN9BXcRA-"
        "uEz24TvTgOtGD60QPrpOpgqcKnip4quCpgnUDmugGNNENaKIb0EQ3oIluQBPdgCa6AU10A5roBjTRDWiiG9BEN6CJbkAT3YAmugFNdAOa6AY00"
        "Q1oohvQRBfGiS6ME10YJ7owTnRhnOjCONGFcaIL40QXxokujBNdnye6Pk90fZ7o-jzR9Xmiq-hEV9GJrqITXUUnXx-lb_9sHP-59FP-o_-"
        "wWL4n-0cHazuxp31UHb3W9w__Q2Zqr-8Kiu2S7vWokuNWH77kLNUnqz7neD14XHnOceX4knOOS86ZvYLmhfmn8J3cp_"
        "sClXSuesK6YW7nqieqG-"
        "Z2rnriumFu56onqRvmdtqFVlOZupi1gKo61cWsBVRVrS5mLaCqhnUxawFVFa2MyT6xVMzUdkXVXXF119e5wsp1Hdat67VhYd0wt7Mk-"
        "7JhbmfJzMqGuZ3Vsy5f18VhpWvWGV0XU71maw5SWq7SNVtzkNLila7ZmoOUlrJ0zdYcxMTYZfhZ3RVVd8XVXV_"
        "niirXdVS3rteGhXXD3M6S7MuGuZ0lMysb5nZWz7p8XReHla5ZZ3RdTPWarTlIablK12zNQUqLV7pmaw5SWsrSNVtzEBNTsa7Xu8Lqrri66-"
        "tcceW6juvW9dqwsG6Y21lS_bJhbmfJzMqGuZ3Vsy5f18VhpWvWGV0XU71maw5SWq7SNVtzkNLila7ZmoOUlrJ0zdYcxMRUrOv1rrC6K6ru-"
        "jpXUrmuk7p1vTYsrBvmdpakWDbM7Sypd9kwt7N61uXrujisdM06o-tiqtdszUFKy1W6ZmsOUlq80jVbc5DSUpau2ZqDmJiKdb3eFVZ3RdVd-b-"
        "0MK97YY_9MG_CxmVhYzfMfK4fV0y5alDZKdb6_uGn4pJP1cWP0yWfo0s-"
        "QHNXyXGrD19yluqTlZ3zn6oP7YWe9YtK6bCwbpjb6ayPqmFup7t2Koa5nc7yrhrmdrJsdSUp61z1VJakrHPVU1mSss5VT2VJyjpXPZUlKessv4"
        "aWjK6LKb-G_uUgpeVau4b-5SClxVu7hv7lIKWlXLuG_uUgtYWtW3J1MWsBfyts5TosBvytsJWrshjwt8JWrtFiwN8KuxZTcpcq74qqu-"
        "LqropzrVXVdlWca60CtuvrXOVfRhV6yq_"
        "X5V9GVQ1zO0sqVfllVOmwuG6Y21lS4covo9aGjetKUtZZ8qKVDSsvybiuJGWdJS922bDykpQthPLrde33cyWj62Kqr8V_-36u7iBVVauLqb4W_"
        "-37ubqDVFW0Lqb6WlxxkMqVV3otrjlIbWHrlmNdTPW1uOYgtYWtW6qVMSUfOMq7ouquuLqr4lxrL03F9Xq9q-JcZl7lX7IWesqv1-"
        "VfslYNcztLKlX5JWvpsLhumNtZUuHKL1nXho3rSlLWWfJWKBtWXpJxXUnKOkte7LJh5SUpWwjl1-va751LRtfFVF-L__"
        "a9c91BqqpWF1N9Lf7b9851B6mqaF1M9bW44iCVK6_0WlxzkNrC1i3Hupjqa3HNQWoLW7dUK2MqrtcVX-"
        "iXd8XVXRXnWntpKh6917sqzmXmVf7Lg0JP-fW6_JcHVcPczpK3UOUvD0qHxXXD3M6SClf-"
        "8mBt2LiuJGWdJa9M2bDykozrSlLWWfJilw0rL0nZQii_Xtf-PqVkdF1M9bX4b79PqTtIVdXqYqqvxX_"
        "7fUrdQaoqWhdTfS2uOEjlyiu9FtccpLawdcuxLqb6WlxzkNrC1i3VypiK63XFL6rKu6Lqropzrb00Fdfr9a6Kc5l5lf9SrNBTfr0u_"
        "6VY1TC3s6Qclb8UKx0W1w1zO0sqXPlLsbVh47qSlHWWvDJlw8pLMq4rSVlnyXIvG1ZekrKFUH69rvqdWFVl6mKqr8U1ByktV-"
        "m1uOYgpcUrvRbXHKS0lKXX4pqD1Ba2bsnVxVRfi2sOUlvYuuVYF1N9La45SG1h65ZqZUzF9Xq9K6zuiqq74vKutZem4nq93lVxLhUnaBV38z8R"
        "_U_p9_32D0jKv-8vjVkLiP6Fg7gxawHxv3AQN2YtIPkXDuLG_L0Y1VWonX71vGsnXD3T2imud_5z3dyx_rUsuPwqusLqrqi6K67u-"
        "kqr5GtM5w-aql83N6Z6idUcxI2pXmI1B3FjqpdYzUFKa1JbjOoq1E6_et61E66eae0U1zuLy9B8ibs-"
        "n4pluN4VVXfF1V1faZV8O2OTKf92pjSm-nJZcxA3pnqJ1RzEjaleYjUHKa1JbTGqq1A7_ep51064eqa1U1zvLC5D893U-"
        "nwqluF6V1TdFVd3faVV8qHTJlP-obM0pnqJ1RzEjam-XNYcxI2pXmI1BymtSW0xqqtQO_3qeddOuHqmtVNc7ywuQ_ORe30-"
        "FctwvSuq7oqru77SKnmWtsmUP0uXxlQvsZqDuDHVS6zmIG5M9eWy5iBuzN-"
        "LUV2F2ulXz7t2wtUzrZ3iemdxGZpPEuvzqViG611RdVdc3bX6u17sW_2xrrMvKtkXl-"
        "z7Ol642vdZsi8q2ReX7Ps6XlRyvKgk56jkeFHJ8eKS48Ulx4tLco5LjpeUHC8pOV5ScrzE5pz__S7-X7Rsdr-aFwdq_"
        "nPdiFft40WhHa7azbNC2y-0C38MfDIptB9W7fZnoV045nnhb2jP71dt_O-E1W6v2vifXKldOM7I_tUlJ_ylXaOc-Ndqazh_"
        "18gCrNz5a0kWYuW-"
        "47fWWZSVP1hvfzrunO88cPzeOou18rZ1Fm3lzvFXxQtt8UJbvNAWL3SKFzrFC53ihU7xQqd4oVO80Cle6BQvdIoXOsULneKFTvFCp3ihU7zQKV"
        "7oFC90ihfZ4kW2eJEtXuQUL3KKFznFi5ziRU7xIqd4kVO8yCle5BQvcooXOcWLnOJFTvEip3iRU7zIKV7kFC-"
        "2xYtt8WJbvNgpXuwUL3aKFzvFi53ixU7xYqd4sVO82Cle7BQvdooXO8WLneLFTvFip3ixU7zYKV5ii5fY4iW2eIlTvMQpXuIUL3GKlzjFS5ziJ"
        "U7xEqd4iVO8xCle4hQvcYqXOMVLnOIlTvESp3hJoXjjVeHGq6KNVwUbF4o1LhRqXCjSuFCgcaE440JhxoWijAsFGReKMS4UYlwowrhQgHFh8uP"
        "CxMeFSY8LEx4XJ2vvjivtGuXEnbvjylkA5-64chbCuTsW_NY6i-LcHVfe_nTcOd954Pi9dRbLuTuunEVz7o4Ft__-"
        "c1W80BYvtMULneKFTvFCp3ihU7zQKV7oFC90ihc6xQud4oVO8UKneKFTvNApXugUL3SKFzrFC53iRbZ4kS1eZIsXOcWLnOJFTvEip3iRU7zIKV"
        "7kFC9yihc5xYuc4kVO8SKneJFTvMgpXuQUL3KKFznFi23xYlu82BYvdooXO8WLneLFTvFip3ixU7zYKV7sFC92ihc7xYud4sVO8WKneLFTvNgp"
        "XuwUL3aKl9jiJbZ4iS1e4hQvcYqXOMVLnOIlTvESp3iJU7zEKV7iFC9xipc4xUuc4iVO8RKneIlTvMQpXuHuiN9A5oVjs_"
        "vVRMH4K8pG4deVKJTa4aqNAqntF9q3qzaKovbDqt3-LLQLx0QR1L5ftTF5tdurNiatduE4I_ubPU7Y3B1Xyonbu2PBWQB7dyw4C2HvjkW_"
        "tc6i2LtjwdvOb__azvlYJHt3LDiLZe-"
        "OBWfR7N2x6Pb3UavihbZ4oS1e6BQvdIoXOsULneKFTvFCp3ihU7zQKV7oFC90ihc6xQud4oVO8UKneKFTvNApXugUL7LFi2zxIlu8yCle5BQvc"
        "ooXOcWLnOJFTvEip3iRU7zIKV7kFC9yihc5xYuc4kVO8SKneJFTvMgpXmyLF9vixbZ4sVO82Cle7BQvdooXO8WLneLFTvFip3ixU7zYKV7sFC9"
        "2ihc7xYud4sVO8WKneLFTvMQWL7HFS2zxEqd4iVO8xCle4hQvcYqXOMVLnOIlTvESp3iJU7zEKV7iFC9xipc4xUuc4iVO8b7ujvxfA2XVyf43q"
        "QXb0v8uyPSFpi8yfZHpi01fbPoS05es-lb7818UUFYB2p39jzhPthnx1cz3FiZk9Ks3tL2h7Y1sb2R7Y9sb297E9iar3p1Vz05hr811x-"
        "a6Y3Pdsbnu2Fx3bK47Ntcdm-"
        "uOzXXH5nq46jks7LW5HtpcD22uhzbXQ5vroc310OZ6aHM9tLke2lyPbFZHNqsjm9WRzerIZnVkszqyWR3ZrI5sVkdOVoWe1Ur-"
        "6tgyHbCGnUfDzqNh59Gw82jYeTTsPBp2Hg07j4adR8POo1HoMfNoFGKK82g9fsWwme81syvoV29oe0PbG9neyPbGtje2vYntXc2u9bTqeVrtna"
        "72Tgt77QymdgZTO4OpncHUzmBqZzC1M5jaGUztDKZ2BrNVz6yw1-Y6s7nObK4zm-vM5jqzuc5srjOb68zmOrO5LlY9i8Jem-"
        "vC5rqwuS5srgub68LmurC5LmyuC5vrwub6tup5K-y1ub7ZXN9srm821zeb65vN9c3m-"
        "mZzfbO5vplc25tfPWzme02uBf3qDW1vaHsj2xvZ3tj2xrY3sb2FXLdWPVuFvTbXLZvrls11y-a6ZXPdsrlu2Vy3bK5bNtctm-"
        "veqmevsNfmumdz3bO57tlc92yuezbXPZvrns11z-a6Z3PdX_XsF_baXPdtrvs2132b677Ndd_mum9z3be57ttc902u-f8l6au3oF-"
        "9oe0NbW9keyPbG9ve2PYmtreYVaGneKdjx5bpyMzb_"
        "YphM99rZlfQr97Q9oa2N7K9ke2NbW9sexPbu5qdt1rL3l5hr83VrmXPrmXPrmXPrmXPrmXPrmXPrmXPrmXPrmVvtZa9_"
        "cJem6tdy55dy55dy55dy55dy55dy55dy55dy55dy97qOc87Kuy1udpnUs8-"
        "k3r2mdSzz6SefSb17DOpZ59JPftM6tlnUu941XNc2GtzPba5Httcj22uxzbXY5vrsc312OZ6bHM9trk2Vz3Nwl6ba9Pm2rS5Nm2uTZtr0-"
        "batLk2ba5Nm2vT5Dr4_dXDZr7X5FrQr97Q9oa2N7K9ke2NbW9sexPbW8j1edXzXNhrc322uT7bXJ9trs8212eb67PN9dnm-"
        "mxzfba5rq7Ig5fCXpurvXsM7N1jYO8eA3v3GNi7x8DePQb27jGwd4-BvXsMXlc9r4W9NtdXm-urzfXV5vpqc321ub7aXF9trq8211eTqz_"
        "66mEz32tyLehXb2h7Q9sb2d7I9sa2N7a9ie0t5LpaA_5LYa_N1a4B364B364B364B364B364B364B364B364Bf_"
        "WZwX8r7LW52s8Mvv3M4NvPDL79zODbzwy-_czg288Mvv3M4NvPDP77que9sNfm-"
        "m5zfbe5vttc322u7zbXd5vru8313eb6bnNdrnqWhb0216XNdWlzXdpclzbXpc11aXNd2lyXNtelzbVwzsL5CucqnCeyM4jsDCI7g8jOILIziOw"
        "MIjuDyM4gsjOI7AwK4wpjYptrbHONba6xzTW2ucY219jmGttcY5trbHIdrVbx6L2w1-Q6sqt4ZFfxyK7ikV3FI7uKR3YVj-"
        "wqHtlVPLKreLRaxaNlYa_N1a7ikV3FI7uKR3YVj-wqHtlVPLKreGRX8ciu4mD1pBgcF_aaXAP7pBjYJ8XAPikG9kkxsE-"
        "KgX1SDOyTYmCfFAP7pBisnhSDZmGvzdU-KQb2STGwT4qBfVIM7JNiYJ8UA_ukGNgnxcA-"
        "KQarT5XBSWGvzfXE5npicz2xuZ7YXE9sric21xOb64nN9cTmer7qOS_stbme21zPba7nNtdzm-"
        "u5zfXc5npucz23uZ7bXLurnm5hr821a3Pt2ly7NteuzbVrc-3aXLs2167NtWtz7a16eoW9NteezbVnc-3ZXHs2157NtWdz7dlcezbXns21v-"
        "rpF_baXPs2177NtW9z7dtc-zbXvs21b3Pt21z7Nldv1eMV9tpcPZurZ3P1bK6ezdWzuXo2V8_"
        "m6tlcPZvrYNUzKOy1uQ5srgOb68DmOrC5DmyuA5vrwOY6sLkObK7DVc-wsNfmOrS5Dm2uQ5vr0OY6tLkOba5Dm-"
        "vQ5jq0uV6sei4Ke22uFzbXC5vrhc31wuZ6YXO9sLle2FwvbK4XNtfLVc9lYa_"
        "N9dLmemlzvbS5XtpcL22ulzbXS5vrpc310uZ6teq5Wu29Xu29_tqrP2OaFtqzQnteaDt_ncQxK585Pnfc-QOd1fjQGR8640NnfOSMj5zxkTM-"
        "csbHzvjYGR8742NnfOKMT5zxiTM-KYwfF8aOC-"
        "PGhTHjYrxT77FT77FT77FT77FT77FT77FT77FT77FT77FT77FT77FT77FT77FT77FT77FT77FT77FT77FT77Gtt_6p5bTQnhXa80Lb-"
        "ReUHGPrXfS5484_IlyND53xoTM-dMZHzvjIGR854yNnfOyMj53xsTM-dsYnzvjEGZ8447_qrT8lPr_5Vvjb4dzQHC-LHamlo_"
        "HL5UGAJn9hQ0NQ46DQI8WXof0fafP0IGveD7NmJ8_jI2uPVrtxRb7M_li5tYmIHv5V9KYOCe0_ot1G-yVvZ_8crP-an7bwb4SbO9_-"
        "P2ob96k");
    static string all_emojis_str = gzdecode(base64url_decode(packed_emojis).ok()).as_slice().str();
    constexpr size_t EMOJI_COUNT = 5064;
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
  auto len = str.size();
  if (len > MAX_EMOJI_LENGTH + 3) {
    return false;
  }
  if (emojis.count(str) != 0) {
    return true;
  }
  if (len <= 3 || str[len - 3] != '\xEF' || str[len - 2] != '\xB8' || str[len - 1] != '\x8F') {
    return false;
  }
  if (len >= 6 && str[len - 6] == '\xEF' && str[len - 5] == '\xB8' && str[len - 4] == '\x8F') {
    return false;
  }
  return emojis.count(str.substr(0, len - 3)) != 0;
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
