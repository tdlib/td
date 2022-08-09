//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
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
        "eJyNnety27i2rV8lVfvf-XXul7frVmfZ6cS6kKJskrLj7tUUaUmOI1uObcUEWLUfJecBdledFzgaJIfJiUkyqyqAJ4wxJzE_QABF2fGPs_"
        "jdj7Plux_T9bFsjmV7LLfHsjuW_bsfF-ZY7LsfYXAsi3c_og_H8vuxfDyWT8dydizjY5kcy_RYZsfiHYt_LPNjKd79iI9f4z-P5Rg_"
        "PsaPjzHjY8zl-2P5x7EcfZfH-Mtj__LhWI7XXX47lu_HctReHjWXx-tdHq93efPux9XxmldHn6tj3Kuj39VxXFcX7358Pn79fH4ssI_j_"
        "3y89u0xt9vLdz--HMf1ZfHu7-tf3h-r36bH6mSCykPlo5qjClBBd3KO6gJViCpCFR-rD7-iKq2vqB5Q7VE9ovqG6gnVC6oDqu_"
        "H6vcZKlzt7BdUiHI2QvUbKozq7B-oTlCdovqA6ndUH1F9QnWGaowKoz9DHmeIfFZGRh5nyOMMeZwhjzPkcYY8zpDHGfI4w-"
        "iPs36sLlFdofqM6hrVH6i-oLpDhSzPdqjuUSHfM-R7hnzPkO_ZMyrke4Z8z5Dv2SuqHJVBZVEVx2qM9MdIf4z0x0h_jPTHSH-M9MdIf4z0x0h_"
        "jPTHSH-M9MdIf4z0x0h_jPTHSH-M9MdIf4z0x0h_jPTHSH-M9MdIf4z0x0h_jPTHSH-M9MdIf4z0x3-i-"
        "ieqv1AlqFaoUlQZqhtUa1QbVFtUt6iAbgx0Y6AbA90Y6MZANwa6MdCNgW6MpTIGvzH4jcFvDH5j8BuD3xjoxkA3AboJ0E2AbgJ0E6CbAN0E6CZ"
        "ANwG6CdBNgG4CdBOgmwDdBOgmQDcBugnQTYBuAnQTpD9B-hOkP0H6E6Q_QfoTpD9B-hOkP0H6E6Q_QfoTpD9B-hOkP0H6E6Q_QfoTpD9B-"
        "hOkP0H6E6Q_QfoTpD9B-hOkP0H6E6Q_wfKZgMEEDKZgMAWDKRhMwWAKBlMwmILBFAymYDAFgykYTJHvFPlOke8U-U6R7xT5TpHvFPlOke8U-"
        "U6R7xT5TpHvFPlOke8U-U6R7xT5TpHvFPlOke8U-"
        "U6R4BQZTZHRFBlNkdEUGU2R0RQZTZHRDBnNkNEMGc2Q0QwZzZDRDBnNkNEMGc2Q0QwZzTCrM8zqDLM6w6zOkOUMWc6Q5QxZzpDlDC-"
        "IGV4QM7wgZnhBzPCCmOEFMcMLYoYXxAwviBleEDO8IGZ4QcxAaAZCMxCagdAMhGYgNAOhGQjNQGgGQjMQmoHQDIRmIDQDoRkIzUBohhUxw4qYY"
        "UXMAGyGFTHDiphhRczAbwZ-M_Cbgd8M_GbgNwM_D-"
        "g8UPNAzQM1D9Q8UPNAzQM1D9Q8UPNAzQM1D9Q8UPNAzQM1D9Q8UPNAzQM1D9Q8UPNAzQM1D9Q8UPNAzQM1D9Q8UPNAzQM1D9Q8UPNAzQM1D9Q8"
        "UPNAzQM1D9Q8UPNAzQM1D9Q8UPNAzQM1D9Q8UPNAzQM1D9Q8UPNAzQM1D9Q8UPNAzQM1D9S8khpWnQ90PladD34--Png54OfD34--"
        "Png54OfD34--Png54OfD34--Png54OfD34--Png54OfD34--Png54OfD34--Png54OfD34--Png54OfD34--Png54OfD34--Png54OfD34--"
        "Png54OfD34--Png54OfD34--Png54OfD34--Png54OfD34--Png54OfD34--M3Bbw5-c_Cbg98c_ObgNwe_"
        "OfjNwW8OfnPwm4PfHPzm4DcHvzn4zcFvDn5z8JuD3xz85uA3B785-M3Bbw5-c_Cbg98c_ObgNwe_"
        "OfjNwW8OfnPwm4PfHPzm4DcHvzn4zcFvDn5z8JuD3xz85uA3B785-M3Bbw5-c_Cbg98c_ObgNwe_"
        "OfjNwW8OfnPwm4PfHPzmoBaAWgBqAagFoBaAWgBqAagFoBaAWgBqAagFoBaAWgBqAagFoBaAWgBqAagFoBaAWgBqAagFoBaAWgBqAagFoBaAWg"
        "BqAagFoBaAWgBqAagFoBaAWgBqAagFoBaAWgBqAagFoBaAWgBqAagFoBaAWgBqAagFoBaAWgBqAagFoBaAWgBqAagFoBaAWoBVt0D6C6S_"
        "QPoLpL9A5gtkvkDmC2S-QOYLZL5A5gtkvkDmC2S-QOYLZL5A5gtkvkDmC2S-QOYLZL5A5gtkvkDmC2S-QOYLZL7A-"
        "M5xjXNc4xySCwz3AsO9wHAv8CK5wHSHmO4Q0x1iukNMd4jpDjHdIaY7xHSHmO4Q0x1iukPkGyLfEPmGyDfEdIdIOkTSIZIOkXSIpEMMKMSAQiQ"
        "dIukQSYdIOkTSIZIOkXSIpEMkHSLpEEmHSDpE0iEyCpF0iKRDJB1iukNMd4jpDjHdIaY7xHSHmO4Q0x1iukNMd4jpDjHdIaY7xHSHmO4Q0x1iu"
        "kNMdwicIfiF4BeCXwh-IfhF4BeBXwR-EfhF4BeBXwR-EfhF4BeBXwR-EfhF4BeBXwR-"
        "EfjFiBcjXox4MeLFiBcjXox4MeLFiBcjXox4MeLFiBcjXox4cRkP8xFjPmLMR4z5iDEfMeYjxnzEmI8Y8xFjPmLMR4z5iDEfMeYjxnzEmI8Y8x"
        "FjPmLMR4z5iDEfMeYjxnzEmI8Y8xFjPmLMR4z5iDEfMeYjxnzEmI8Y8xFjPmLMR4z5iDEfMeYjxnzEmI8Y8xFjPmLMR4z5iDEfMeYjxnzEmI8l"
        "-C3Bbwl-S_Bbgt8S_JYgtASSJZAsgWQJEEuAWALEEvkuke8S-"
        "S4x8CUGvsSolhjVEqNaYlRLjGqJUS0xqiVGtcSoroHpGpiugekamK6B6RqYroHpGpiugekamK6B6RpXuwaSBCNNMJcJ5jLBXCYYeIKBJxh4grl"
        "MMJcJUkiQQoIUEsxlgrlMMJcJ5jLBXCbILUFuCXJLMMgEg0wwyASDTDDIBINMMMgEg0wwyASDTDDIBINMgCTBXCaYywRzmZQDx1wmmMsEc5mAW"
        "gJqCagloJaAWgJqCaglAJZgGhNMY4JpXGEaV5jGFaZxhWlcYRpXmMYVXgErvAJWeAWs8ApY4RWwArUVqK1AbQVqK1BbgdoK1FagtgK1FaitQG0"
        "FaitQW4HaCtRWoLYCtRWorUBtBWorUFuB2grUVqC2ArUVqK1AbQVqK1BbgdoK1FagtgK1FaitQG0FaitQW4HaCtRWoLYCtRWorUBtBWorUFuB2"
        "grUVlhrK6BbAd0K6FZAlwJdCnQp0KVAlwJdCnQpdpAU_FLwS8EvBb8U_FLwS8EvBb8U_FLwS8EvBb8U_FLwS8EvBb8U_FLwS8EvBb8U_"
        "FLwS8EvBb8U_FLwS8EvBb8U_FLwS8EvBb8U_FLwS8EvBb8U_FLwS8EvBb8U_FLwS8EvBb8U_FLwS8EvBb8U_FLwS8EvBb8U_FLwS8EvBb8U_"
        "DLwy8AvA78M_DLwy8AvA78M_DLwy8AvA78M_DLwy8AvA78M_DLwy8AvA78M_DLwy8AvA78M_DLwy8AvA78M_DLwy8AvA78M_DLwy8AvA78M_"
        "DLwy8AvA78M_DLwy8AvA78M_DLwy8AvA78M_DLwy8AvA78M_DLwy8AvA78M_DLwy8AvA78M_DLwy8AvA78M_NaIt0a8NeKtEW-"
        "NeGsEWCPAGgHWCLBGgA2Ib0B8A-IbEN-A-AbENyC-"
        "AcQNIG4AcQOIG0DcAOIGEDeAuAHEDSBuAHEDiBtA3ADiBhA3gLgBxA0gbgBxA4gbQNwA4gYQN4C4AcQNIG4AcQOIG0DcIMsNstwgyw2y3CDLDa"
        "htQG0DahskvUHSGyS9RapbpLpFqlukukWqW6S6RZZbZLlFlltkuUWWW2S5RZZbZLlFlltkuUUeW-SxRR5b5LFFHlvksUUeW-"
        "SxxXC3GO4Ww91iuFsMd4vhbp_e_fv6-O8_Xqbv_v3u-A_Gj19ylNL89XeU0nz_"
        "T5TKPKCU5kmAUpkLlMo8R6nMC5TKDFEqM0KpzDVKZW5QSvPsBqU0p2WpzC8olXmHUplfUSrzHqUyH1Aq8wWlMg8olfkdpTSP7xOPpTTPNyiVuU"
        "WpzCeU0rz4BaUyX1EqM0cpzeOd9bFU5q8olTlCqczfUCrzPUplTlAq00OpzBClMq9QKvMPlMr8J0pl_oVSmSlKZW5QKvMOpTK_"
        "olTmC0plHlAq8ztKaR5vdY-lMkcolXmNUpl_oFTmXyiVuUKpzBSlMm9QKvMVpTItSmkeb1ePpTIDlMpcoFTmOUplXqBUZoRSmUuUyrxEqcw_"
        "UCozQ6nMHUpl3qOU5vIDSmWWpTI9lMqco1TmGqUydyiVeY9SmY8olfmMUpkvKJV5QCnNyxFKZX5AqczfUSrzDKUyxyiVWZbK9FEqM0CpzHOUyr"
        "xCqcw_USpzj1KZjyilefUepTJPUSrzL5TKTFBK8_OfKKV5vPc7lsr8hlKat_9AqcwTlMo8RYH5f3_ZoVSmQSnNjxcolRmhwPz7-h-"
        "7sqob92VVN2xZ1Y2irKrGh1FZ1Y3nsqoaZ3-"
        "WVd1IyqpurMqqbqRlVTeysqobN2VVN9ZlVTc2ZVU3tmVVN27Lqm48lVXVGJuyqhqT87KqGxdlVTeisqobcVnVjWVZ1Y3PZVU3rsuqakw_"
        "lVXdOCurujEuq7oxKau6EZRV3ViUVd04L6u6cVFWdSMsq7oRlVXdiMuqbizLqm5cllXduCqruvG5rOrGdVnVjX1Z1Y1vZVU3nsuqaszKZ_"
        "v1ovB-LauqMTdlVTUWv5dV3fhYVnXja1nVjV1Z1Y19WdWNx7KqG9_Kqm48lVXdeC6ruvFSVnXjUFZV4_y0rOrGx7KqG5_"
        "Kqm6clVXdGJdV3ZiVVd1YlVXduCmrunFfVnXjoazqRl5WVeNiVFZ147eyqhvvy6pueGVVN_yyqhvzsqobl2VVN67Kqm58Lqu68WdZ1Y2_"
        "yqpu3JRV3fhaVnVjX1Z143tZVY3lp7KqG-OyqhuTsqobVVU3_iiruvFnWdWNf5ZV3firrOpGUlZ1Y1VWdWNdVnVjV1Z1Y19WaPzbj9_-"
        "evdvRwtf_1NZ1Y3_XFZ147-UVd34r2VVN_5bWdWN_15WdeN_lFXd-J9lVTf-V1nVjf9dVnXj_5RV3fj7-"
        "jQ9lhsaaxobGlsatzTuaNzTeKDxSOOJxjONFxoHGt9p5DQMDbzATzM0aGQ01jQ2NLY0bml8oXFH4yuNexoPNPY0Hmk80Xim8ULjQOOVRk7D0ig"
        "Hf8PB3xDvDQd_wzHfcMw3HPMNxwxjR-OexgONPY1HGt9oPNP4TuOVRk7D0LA0yjGvSXXNga2Jbs3xrDmMNa--pvuGKW-"
        "Y8oYBNwy4YaYbDnVDvBvi3XDwW9LYchhbDmPLYWw5jC0D3nIYt1w2tyR_y_HccgpuObBbDuyWF70l-"
        "Vte65bkbwn8lqvltn31FxoHGt9p5DRK8l-Yzhde4gsv8YUBvzDOF8a5I9475nXHvO445jsGvGPAO4K645jveIk7jvmO1_"
        "rKgF8Z5yvdvzL3HTU7MtyR4Y4Md3TfcRi7tvszjZyGpVGuqHtO5T2n8p653_MS92R4z4D3TOee6dwTHYxXGuW1HniJB0Z-INUHJvjA1fLATB-"
        "Y6QOv_kDyD0z5gSk_EN0Dc3_gFDxwzA8c8wPH_"
        "MAxP3DMDwQFw9B4y6Iktmc6e6azZxZ7ZrFnFnsy3HPwew51z6HuOcI9x7PntR6Z6Tde9Buv9Y3X-sZrfSOxbyT2jRf91o6zp_"
        "FM44XGgUZOo8z9iVd_5tWfmcUz3Z85-Ge6v9ALRkbjhsaaxobGLY0vNO5ofKWxo3FP44HGnsYjjWcaLzQONF5pGBqWRkn-wMEfOOYDx3wg-"
        "QPHfOCYDxzqgUM9cKgHDvXAoR441AOHeuAIDxzhgTAPHNh3Duw7r_6dkb8z8nem_J15faf7K91fmdcrp-CVAV9J_"
        "pUBXzm5OXPPeQnDTC3jWGZR8FoFR1jU6Uwf_98fv2bH8s9jWR3LXd0u3M79sfx1LI9dnc_HcluLCjz6KX9Qqfqa119N_dXWXws8Sah05de8_"
        "mrqr7b-WuDHNitd-TWvv5r6q62_Qvep1n2qdZ9q3ada96nWndW6s1p3VuvOat1ZrRvXunGtG9e6ca0b1z98NflH_ZNZpZHTMDQsjZLciD_"
        "GNeKPco3441wj_kjXiOLfKP6N4t8o_o3i3yh-T_F7it9T_"
        "J7i9xSfUnxK8SnFpxSfUvyR4o8Uf6T4I8UfKf5E8SeKP1H8ieJPFJ9RfEbxGcVnFJ_VYo_oPKLziM4jOo_oPKLziM4jOo_oPKLzTig-"
        "ofiE4hOKTygmOo_oPKLziM4jOu8DxR8o_kDxB4o_UPw7xb9T_DvFv1P8O8WcFI-T4nFSPE6Kx0nxOCkeJ8XjpHicFI-"
        "T4nFSPE6Kx0nxOCne26SMKR5TPKZ4TDFfKd6E4gnFE4onFE8onlI8pXhK8ZTiKcUzimcUzyieUTyjOKU4pTilOKU4pTijOKM4ozijOKP4huIbi"
        "m8ovqH4huI1xWuK1xSvKV5TvKV4S_GW4i3FW4pvKb6l-JbiW4pvKf5C8ReKv1D8heIvFN9RfEfxHcV3FN9RvKN4R_GO4h3FO4rvKb6n-"
        "J7ie4rvKX6g-IHiB4ofKH6geE_xnuI9xXuK9xQ_UvxI8SPFjxQ_UvyN4m8Uf6P4G8XfKH6i-IniJ4qfKH6i-JniZ4qfKX6m-JniF4pfKH6h-"
        "IXiF4pzinOKc4pzivNa7P9ai0sjp2FoWBqlmPuzz_3Z5_7sc3_2uT_73J997s8-92ef-7PP_dnnCevzhPV5wvo8YX2esD43c5-buc_"
        "N3Odm7nMz97mZ-"
        "9zMfW7mPjdzn5u5zx3J547kc0fyuSP53JF8j2KPYo9ij2KP4g3FG4o3FG8o3tTiBZfogkt0wSW64BJdcIkuuEQXXKILLtEFl-iCS3TxneLvFH-"
        "n-DvF32vx-awWl0ZOw9CwNErxguIFxQuKFxQvKD6n-"
        "Jzic4rPKT6vxRHXRsS1EXFtRFwbEddGxLURcW1EXBsR10bEtRFxbURcGxHXRsS1EXFtRDxhI56wEU_"
        "YiCdsxBM24gkb8YSNeMJGPGEjnrART9iIJ2zEEzbiCRvxhI14wkY8YSOesBFP2IgnbMT1HHE9R1zPEddzxPUc_"
        "1WLSyOnYWhYGqWYSzTmEo25RGMu0ZhLNOYSjblEYy7RmEs05hKNuYvG3EVj7qIxd9GYu-"
        "jyl1pcGjkNQ8PSKMWclCUnZclJWXJSlpyUhOKE4oTihOLkTUzOCTkn5JyQc0LOSUhxSHFIcUhxSHFEcURxRHFEcURxTHFMcUxxTHFM8ZLiJcVL"
        "ipcULym-pPiS4kuKLym-pPiK4iuKryi-"
        "oviK4s8Uf6b4M8WfKf5M8TXF1xRfU3xN8TXFvAlMeBOY8CYw4U1gwpvAZEfxjuIdxTuKdxTzTibhnUzCO5mEdzIJ72QS3skkvJNJeCeT8E4m4Z"
        "1MwjuZhHcyCe9kEt7JJLyTSfgaTPgaTPgaTPgaTPgaTPgaTPgaTPgaTPgaTPgaTPgaTPgaTPgaTPgaTPgaTHgnk_"
        "BOJuGdTMI7mYR3MgnvZBLeySS8k0l4J5PwTiY5UHyg-"
        "EDxgeIDxYZiQ7Gh2FBsKLYUW4otxZZiW4tXTHDFBFdMcMUEV0wwJeeUnFNyTsk5JeeUnFNyTsk5JeeUnFOiS4kuJbqU6FKiS4kuJbqU6FKiS4k"
        "ufaX4leJXil8pfq3FGU-rjKdVxtMq42mV8bTKeFplPK0ynlYZT6uMp1XGXTTjLppxF824i2bcRTPefWW8-8p495Xx7ivj3VfmU-xT7FPsU-"
        "xTPKd4TvGc4jnFc4oDigOKA4oDigOKeY-U8R4p4z1SxnukjPdIGe-RMt4jZbxHyniPlPEeKbug-"
        "ILiC4ovKL6gmAdQxgMo4wGU8QDKeABlPIAyHkAZD6CMB1DGAyjjAZTxAMp4AGU8gDIeQBkPoIwHUMYDKOMBlPEAyngAZTyAMh5AGQ-"
        "gjAdQxgMo4wGU8QDKeABlPIC2fJ-y5fuULd-nbPk-Zcv3KVs-gtvyEdyWj-"
        "C2fAS35SO4LW9ct7xx3fLGdcsb1y1vXLc7incU7yjeUbyjmAfQlgfQlgfQlgfQlgfQlgfQlgfQlgfQlgfQlgfQlgfQlgfQlgfQlgfQlgfQlgfQ"
        "lgfQlgfQlgfQlgfQlhvjlhvjlhvjlhvjlhvjlhvjlhvjlhvjlhvj9u2t9M2PX8Y_rpJj_Xf9q5L1d_CjAeqb5Xf8T_1q1fd3_"
        "auYtPW3srbd0a1VHXH7w3dcpf9i_ddca_G695rrXv-Oa647rokZFBPzd-vJ2as7QR2dTU8-"
        "5OZ2Nj1myM3tbHrskJvb2fQUQ25up1xoA2SGNErQx2lIowR91IY0StDHcEijBH1EezV4X9GTqewy_V22v-"
        "vtWnnvus6H1rVyy4fc3M6O0Xe5uZ0dmXW5uZ39WXev67Zb55p1vIc0_Wt2IEgnrs41OxCkE17nmh0I0omyc80OBBEauQxf-"
        "7tMf5ft73q7luld12ZoXSu3fMjN7ewYfZeb29mRWZeb29mfdfe6brt1rlnHe0jTv2YHgnTi6lyzA0E64XWu2YEgnSg71-xAEKHpWde6K-_"
        "vsv1db9eyvevaDq1r5ZYPubmdHfS73NzOjsy63NzO_qy713XbrXPNOt5Dmv41OxCkE1fnmh0I0gmvc80OBOlE2blmB4IITc-"
        "61l15f5fp73q7VtG7rouhda3c8iE3t7NjiF1ubmcH7y43t7M_6-"
        "513XbrXLOO95Cmf80OBOnE1blmB4J0wutcswNBOlF2rtmBIELTs651V97fZfq7qp-HEPPe-o58My9k6y7Z2pWJ9_XrnpT7nLouofr-"
        "rt8Vd7yrbr-d7ngf3fEGuv5WR9z-8B1X6b9Y1zX_7nvT3urRm0qnWz7k5nY666PPze10106Pm9vpLO8-"
        "N7ezxjaEpKuz6elF0tXZ9PQi6epsenqRdHU2Pb1Iujq799AO7yFN9x76kyCduNQe-pMgnfDUHvqTIJ0o1R76kyCDYIeW3JBGCX4GtncdtgU_"
        "A9u7KtuCn4HtXaNtwc_AKk3HKdXdZfq7bH9Xz7UUVdnVcy1FQHa9Xav7YVSrp3u_7n4Y1efmdnaQ6n0Y1elmh9zczg7CvQ-"
        "jlNt6CElXZ8ekdbl1I1kPIenq7JjsLrduJF0LoXu_Hnw-1-E9pOnfi3_2fG4oSB-1IU3_Xvyz53NDQfqIDmn69-KeIL0rr3MvHggyCHZoOQ5p-"
        "vfigSCDYIeWaq-m4w1Hd5fp77L9XT3XUlPTs1_rrp5riby6H7K2err36-"
        "6HrH1ubmcHqd6HrJ1udsjN7ewg3PuQVbmth5B0dXa8FLrcupGsh5B0dXZMdpdbN5KuhdC9Xw8-d-7wHtL078U_e-48FKSP2pCmfy_-"
        "2XPnoSB9RIc0_XtxT5Delde5Fw8EGQQ7tByHNP178UCQQbBDS7VX07Nf9zzQ7-6y_V0911JT03Prrbt6riXy6v7woNXTvV93f3jQ5-"
        "Z2dryEej886HSzQ25uZwfh3g8PlNt6CElXZ8fMdLl1I1kPIenq7JjsLrduJF0LoXu_Hvw8pcN7SNO_F__s85ShIH3UhjT9e_"
        "HPPk8ZCtJHdEjTvxf3BOldeZ178UCQQbBDy3FI078XDwQZBDu0VHs1Pft1zwdV3V2mv6vnWmpqevZr3dVzLZFX94dirZ7u_br7Q7E-"
        "N7ezA0fvh2KdbnbIze3sINz7oZhyWw8h6ersmJkut24k6yEkXZ0dy73LrRtJ10Lo3q_7PhPrIzOk6d-"
        "LB4J04urciweCdMLr3IsHgnSi7NyLB4IMgh1ackOa_r14IMgg2KHlOKTp34sHggyCHVqqvZqe_Vp35f1dpr_LdnepqenZr3VXz7UIJ_Pa367-"
        "h-ry9zo0L_lrHt3P-zs1SmD-hSCuRgnsvxDE1ShB8S8EcTU_h9FPYTD9_rwHE-7PdDBF3Vn_lbJW-21Z1Muvpyvv7zL9Xba_"
        "621YHY8xnV876p83V9O_xAaCuJr-JTYQxNX0L7GBIJ1MBmH0UxhMvz_vwYT7Mx1MUXe2l6F4iKvz6VmGusv0d9n-"
        "rrdhdTydkYPpfjrTqenfLgeCuJr-JTYQxNX0L7GBIJ1MBmH0UxhMvz_vwYT7Mx1MUXe2l6F4NqXz6VmGusv0d9n-"
        "rrdhdbzplIPpftPZqelfYgNBXE3_djkQxNX0L7GBIJ1MBmH0UxhMvz_vwYT7Mx1MUXe2l6F4y63z6VmGusv0d9n-"
        "rrdhddxLy8F030t3avqX2EAQV9O_xAaCuJr-7XIgiKv5OYx-CoPp9-c9mHB_poMp6s72MhTvJHQ-PctQd5n-Ltvf1fz2bfm95ldqne-Zju_"
        "Zju-9xcub7712fM90fM92fO8tnumIZzrGbDrimY54tiOe7YhnO8ZsO-IVHfGKjnhFR7xCjrn6_d3yr8nR_I_qP9SvWucts91x-"
        "aFltjrqP0Rd2-XfYqadN3b5h5BpJy279WvD5R8Apv2lsf3Xlt2KGbR-27b8S5G1Xf5JPtp-Y5d_fYp2K04qfz-zRtM0mezbd86dpiuoUTVNR_"
        "CG7K1dY2vazm9j1viaduK0b2S7Rtm0v8i2_-q0nesFmdO-le0acdP2ZbtG3bSd-"
        "A3yXCLPFfJcIs8V8lwizxXy3EGeO8hzB3nuIM8d5LmDPHeQ5w7y3EGeO8hzB3nuIM8d5LmDPHeQ5w7y3EFuJHKjkBuJ3CjkRiI3CrlxkBsHuXG"
        "QGwe5cZAbB7lxkBsHuXGQGwe5cZAbB7lxkBsHuXGQGwe5cZBbidwq5FYitwq5lcitQm4d5NZBbh3k1kFuHeTWQW4d5NZBbh3k1kFuHeTWQW4d5"
        "NZBbh3k1kFuHeSFRF4o5IVEXijkhUReKOSFg7xwkBcO8sJBXjjICwd54SAvHOSFg7xwkBcO8sJBXjjICwd54SAvHORFC_"
        "m6wb0WqNcN5rVAvG7wrgXadQvruoV03cK5bqFctzCuWwjXLXzrFrp1C9u6hWzdwrVuoVq3MK1biNYtPOsWmnUbi7yraDWZrLyraDVdQY1K3VXw"
        "O2_InLuKVtv5sfoan3NX0WrfyHaN0rmraNr-"
        "q9N2rhdkTvtWtmvEzl1F065RO3cVrbb8udwGea6Q5xJ5rpDnEnmukOcO8txBnjvIcwd57iDPHeS5gzx3kOcO8txBnjvIcwd57iDPHeS5gzx3kO"
        "cOciORG4XcSORGITcSuVHIjYPcOMiNg9w4yI2D3DjIjYPcOMiNg9w4yI2D3DjIjYPcOMiNg9w4yI2D3ErkViG3ErlVyK1EbhVy6yC3DnLrILcO"
        "cusgtw5y6yC3DnLrILcOcusgtw5y6yC3DnLrILcOcusgLyTyQiEvJPJCIS8k8kIhLxzkhYO8cJAXDvLCQV44yAsHeeEgLxzkhYO8cJAXDvLCQV"
        "44yAsHeeEgb91VlJ94V7hpVqDq1nnLbHeUeGm2OmqstV0ipZ03domSdtKybxq7xEf7S2P7ry27FbPERfu2sUtMtP3GLvHQbsVJ5WfONRr3rqL1"
        "nXOn6QpqVO5dxdt33pDJu4p22_lIucYn7yra7RvZrlHKu4pW23c-"
        "zfad69Vo5V1Fq10jlncVrXaNWt5VtNvy89UGea6Q5xJ5rpDnEnmukOcO8txBnjvIcwd57iDPHeS5gzx3kOcO8txBnjvIcwd57iDPHeS5gzx3kO"
        "cOciORG4XcSORGITcSuVHIjYPcOMiNg9w4yI2D3DjIjYPcOMiNg9w4yI2D3DjIjYPcOMiNg9w4yI2D3ErkViG3ErlVyK1EbhVy6yC3DnLrILcO"
        "cusgtw5y6yC3DnLrILcOcusgtw5y6yC3DnLrILcOcusgLyTyQiEvJPJCIS8k8kIhLxzkhYO8cJAXDvLCQV44yAsHeeEgLxzkhYO8cJAXDvLCQV"
        "44yAsHeeEgf7urqP_QFgDhD6S3W8DV_sZItpruXHjnrncuvHPX2whv43ob4W1cbyu8retthbd1vQvhXbjehfAuhHfjWX302Dg2Tuyp_"
        "7zw9LfaqTFFx6hlsqM1ObKpBCOn2RLkMkKuIuQyQq4iGBnBqAhGRjAqgpURrIpgZQSrIhQyQqEiFDJCISK8b7zfC8_"
        "3jdd76SHZt5pKMHKaLUEuI-QqQi4j5CqCkRGMimBkBKMiWBnBqghWRrAqQiEjFCpCISNI9h8b74_"
        "C82Pj9VF6SPatphKMnGZLkMsIuYqQywi5imBkBKMiGBnBqAhWRrAqgpURrIpQyAiFilDICJL9J0my1VSCkdNsCXIZIVcRchkhVxGMjGBUBCMjG"
        "BXByghWRbAyglURChmhUBEKGcEh2fIWe_"
        "5b30j00e9MzkCrqQQjp9kS5DJCriLkMkKuIhgZwagIRkYwKoKVEayKYGUEqyIUMkKhIhQygpyBs5a3OwNnLUdnBry7Nz-"
        "aomPUMtkh5qzdVIKR02wJchkhVxFyGSFXEYyMYFQEIyMYFcHKCFZFsDKCVREKGaFQEQoZQcyZ97Xx_"
        "io8vzZeX4XHrvHYCY9d47GTHnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq27hvve-"
        "F533jdSw_J_l6xv5fs7xX7e8n-XrG_l-zvFft7yf5esb-X7O8V-3vJ_l6xv5fs7xX7e8n-XrG_l-zvFft9470XnvvGay89JPu9Yr-X7PeK_"
        "V6y3yv2e8l-r9jvJfu9Yr-X7PeK_V6y3yv2e8l-r9jvJfu9Yr-X7PeK_XPj_Sw8nxuvZ-kh2T8r9s-S_bNi_yzZPyv2z5L9s2L_LNk_K_"
        "bPkv2zYv8s2T8r9s-S_bNi_yzZPyv2z5L9s8ve__"
        "XNm6boGLVMdgj27aYSjJxmS5DLCLmKkMsIuYpgZASjIhgZwagIVkawKoKVEayKUMgIhYpQyAiS_ajxHgnPUeM1kh6S_UixH0n2I8V-"
        "JNmPFPuRZD9S7EeS_UixH0n2I8V-JNmPFPuRZD9S7EeS_UixH0n2I8X-pPE-EZ4njdeJ9JDsTxT7E8n-RLE_kexPFPsTyf5EsT-R7E8U-xPJ_"
        "kSxP5HsTxT7E8n-RLE_kexPFPsTyf5EsT9tvE-F52njdSo9JPtTxf5Usj9V7E8l-1PF_lSyP1XsTyX7U8X-VLI_VexPJftTxf5Usj9V7E8l-"
        "1PF_lSyP3XZV39z-C1Cu6kEI6fZEuQyQq4i5DJCriIYGcGoCEZGMCqClRGsimBlBKsiFDJCoSIUMoJDsuXtvB-u-0air_aL_"
        "vHmR1N0jFomO8SctZtKMHKaLUEuI-"
        "QqQi4j5CqCkRGMimBkBKMiWBnBqghWRrAqQiEjFCpCISOIOYuaXT8Su37U7PrRifSQ7NWuH8ldP1K7fiR3_Ujt-pHc9SO160dy14_Urh_"
        "JXT9Su34kd_1I7fqR3PUjtetHcteP1K4fyV0_Urt-1Oz6kdj1o2bXj06lh2Svdv1I7vqR2vUjuetHateP5K4fqV0_krt-"
        "pHb9SO76kdr1I7nrR2rXj-SuH6ldP5K7fqR2_Uju-pHa9aPm6SlN0TFqmeyQ7NWT60g-uY7Uk-tIPrmO1JPrSD65jtST60g-uY7Uk-"
        "tIPrmO1JPrSD65jtST60g-uY7Uk-tIPrmO1JPrSD65jtST62jceI-F57jxGksPyX6s2I8l-7FiP5bsx4r9WLIfK_ZjyX6s2I8l-"
        "7FiP5bsx4r9WLIfK_ZjyX6s2I8l-7FiP2m8J8Jz0nhNpIdkP1HsJ5L9RLGfSPYTxX4i2U8U-4lkP1HsJ5L9RLGfSPYTxX4i2U8U-"
        "4lkP1HsJ5L9xGUf__XmTVN0jFomOwT7dlMJRk6zJchlhFxFyGWEXEUwMoJREYyMYFQEKyNYFcHKCFZFKGSEQkUoZATJ_rHxfhSej43Xo_"
        "SQ7B8V-0fJ_lGxf5TsHxX7R8n-UbF_lOwfFftHyf5RsX-U7B8V-0fJ_lGxf5TsHxX7R8n-UbFv3hfQFB2jlskOyV69J2t9Z-"
        "Q0W4JcRshVhFxGyFUEIyMYFcHICEZFsDKCVRGsjGBVhEJGKFSEQkaQ7J8a7yfh-dR4PUkPyf5JsX-S7J8U-yfJ_kmxf5LsnxT7J8n-SbF_"
        "kuyfFPsnyf5JsX-S7J8U-yfJ_"
        "kmxf5Lsn1z2SfrmTVN0jFomOwT7dlMJRk6zJchlhFxFyGWEXEUwMoJREYyMYFQEKyNYFcHKCFZFKGSEQkUoZATJvtlzErHnJM2ek3yTHpK92nM"
        "Sueckas9J5J6TqD0nkXtOovacRO45idpzErnnJGrPSeSek6g9J5F7TqL2nETuOYnacxK55yRqz0maT68S8elV0nx6lTxLD8lefXrV-"
        "s7IabYEuYyQqwi5jJCrCEZGMCqCkRGMimBlBKsiWBnBqgiFjFCoCIWMINm_NN4vwvOl8XqRHpL9i2L_Itm_KPYvkv2LYv8i2b8o9i-S_Yti_"
        "yLZvyj2L5L9i2L_Itm_KPYvkv2LYv8i2b8o9ofG-yA8D43XQXpI9gfF_iDZHxT7g2R_UOwPkv1BsT9I9gfF_iDZHxT7g2R_"
        "UOwPkv1BsT9I9gfF_"
        "iDZHxT7FgOZfyt3mXcrZ5lvK1eZp5GzZdRsGTlbRs2WkbNl1GwZOVtGzZaRs2XUbBk5W0bNlpGzZdRsGTlbRs2WkbNl1GwZOVtGzVbr-"
        "vLarevKa1rJ3ir2VrK3ir2V7K1ibyV7q9hbyd4q9layt4q9leytYm8le6vYW8neKvZWsrcu-7Q5IVJxQqTNCZG-SA_"
        "BPlUnRCpPiFSdEKk8IVJ1QqTyhEjVCZHKEyJVJ0QqT4hUnRCpPCFSdUKk8oRI1QmRyhMiVSdEKk-"
        "IVJ0QaXNCpOKESJsTIj1ID8lenRCpPCFSdUKk8oRI1QmRyhMiVSdEKk-"
        "IVJ0QqTwhUnVCpPKESNUJkcoTIlUnRCpPiFSdEKk8IVJ1QmTNU2eaomPUMtkh2LebSjBymi1BLiPkKkIuI-"
        "QqgpERjIpgZASjIlgZwaoIVkawKkIhIxQqQiEjSPbNU2eaomPUMtkh2aunzq3vjJxmS5DLCLmKkMsIuYpgZASjIhgZwagIVkawKoKVEayKUMgI"
        "hYpQyAiS_bTxFh_J161Ry2SHZD9V7KeS_VSxn0r2U8V-KtlPFfupZD9V7KeS_VSxn0r2U8V-KtlPFfupZD9V7KeS_"
        "VSxDxrvQHgGjVcgPST7QLEPJPtAsQ8k-0CxDyT7QLEPJPtAsQ8k-0CxDyT7QLEPJPtAsQ8k-0CxDyT7QLE_b7zPhed543UuPST7c8X-XLI_V-"
        "zPJftzxf5csj9X7M8l-3PF_lyyP1fszyX7c8X-XLI_V-zPJftzxf5csj9X7C8a7wvhedF4XUgPyf5Csb-Q7C8U-wvJ_kKxv5DsLxT7C8n-QrG_"
        "kOwvFPsLyf5Csb-Q7C8U-wvJ_kKxv5DsLxT7sPEOhWfYeIXSQ7IPFftQsg8V-1CyDxX7ULIPFftQsg8V-1CyDxX7ULIPFftQsg8V-"
        "1CyDxX7ULIPFfuo8Y6EZ9R4RdJDso8U-0iyjxT7SLKPFPtIso8U-0iyjxT7SLKPFPtIso8U-0iyjxT7SLKPFPtIso8U-"
        "7jxjoVn3HjF0kOyjxX7WLKPFftYso8V-1iyjxX7WLKPFftYso8V-1iyjxX7WLKPFftYso8V-1iyjxX7ZeO9FJ7LxmspPST7pWK_"
        "lOyXiv1Ssl8q9kvJfqnYLyX7pWK_lOyXiv1Ssl8q9kvJfqnYLyX7pWK_lOyXiv1l430pPC8br0vpIdlfKvaXkv2lYn8p2V8q9peS_"
        "aVifynZXyr2l5L9pWJ_KdlfKvaXkv2lYn8p2V8q9peS_aVif9V4XwnPq8brSnpI9leK_ZVkf6XYX0n2V4r9lWR_pdhfSfZXiv2VZH-"
        "l2F9J9leK_ZVkf6XYX0n2V4r9lWR_pdh_brw_C8_Pjddn4XHdeFwLj-vG47rtwf-Cftey71v2Q8t2_o_42qdp3zvtB6ft_IfnjX_u-OeOf-"
        "74G8ffOP7G8TeOv3X8reNvHX_r-BeOf-H4F45_0fJft3zXLb91y2fd1ju81w7vtcN77fBeO7zXDu-1w3vt8F47vNcO77XDe-"
        "3wXju81w7vtcN77fBeO7zXDu-1w3stefO_Ldy17PuW_dCynf9XsPaRvNvtB6ft_Cd5jX_u-OeOf-74G8ffOP7G8TeOv3X8reNvHX_r-BeOf-"
        "H4F47_G2_-wZhg9a71F2KqVmmuD-2OY-voXf6GdZyVZtVzbFW7U9n39_XZh1Ynm-WPMoZ_tMzKZ_YBrdslzEU1sO-wyyPm6n3LrHfAX0vRRfn_"
        "k_7Ka1zcVL3hXdn00Rt-q2z8H2PhUzUUYdd7bf2_db5_9_8BRjaqUQ");
    static string all_emojis_str = gzdecode(base64url_decode(packed_emojis).ok()).as_slice().str();
    constexpr size_t EMOJI_COUNT = 4682;
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

string remove_emoji_modifiers(Slice emoji) {
  string result = emoji.str();
  remove_emoji_modifiers_in_place(result);
  return result;
}

void remove_emoji_modifiers_in_place(string &emoji) {
  static const Slice modifiers[] = {u8"\uFE0F" /* variation selector-16 */,
                                    u8"\u200D\u2640" /* zero width joiner + female sign */,
                                    u8"\u200D\u2642" /* zero width joiner + male sign */,
                                    u8"\U0001F3FB" /* emoji modifier fitzpatrick type-1-2 */,
                                    u8"\U0001F3FC" /* emoji modifier fitzpatrick type-3 */,
                                    u8"\U0001F3FD" /* emoji modifier fitzpatrick type-4 */,
                                    u8"\U0001F3FE" /* emoji modifier fitzpatrick type-5 */,
                                    u8"\U0001F3FF" /* emoji modifier fitzpatrick type-6 */};
  size_t j = 0;
  for (size_t i = 0; i < emoji.size();) {
    bool is_found = false;
    for (auto &modifier : modifiers) {
      auto length = modifier.size();
      if (i + length <= emoji.size() && Slice(&emoji[i], length) == modifier) {
        // skip modifier
        i += length;
        is_found = true;
        break;
      }
    }
    if (!is_found) {
      emoji[j++] = emoji[i++];
    }
  }
  emoji.resize(j);
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
