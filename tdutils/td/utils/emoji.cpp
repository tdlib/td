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
        "eJyNnety27i2rV8lVfvf-XXul7frVmfZ6cS6kJJskrLj7tUUaYmOI1uObcUEWLUfJecBdledFzgaJIfJiUkyqyqAJ4wxJzE_QABF2fGPs-"
        "jdj7PVux_TzbFsjyU7lttj2R3L_t2PC3Ms9t2PYHEsy3c_wg_H8vuxfDyWT8dydizjY5kcy_RYZsfiHYt_LPNjKd79iI5foz-P5Rg_"
        "OsaPjjGjY8zV-2P5x7EcfVfH-Ktj_-rhWI7XXX07lu_HctReHjWXx-tdHq93efPux9XxmldHn6tj3Kuj39VxXFcX7358Pn79fH4ssI_j_"
        "3y89u0xt9vLdz--HMf1Zfnu7-tf3h-r36bH6mSCykPlo5qjWqCC7uQc1QWqAFWIKjpWH35FVVpfUT2g2qN6RPUN1ROqF1QHVN-P1e8zVLja2S-"
        "oEOVshOo3VBjV2T9QnaA6RfUB1e-oPqL6hOoM1RgVRn-GPM4Q-ayMjDzOkMcZ8jhDHmfI4wx5nCGPM-RxhtEfZ_"
        "1YXaK6QvUZ1TWqP1B9QXWHClme7VDdo0K-Z8j3DPmeId-zZ1TI9wz5niHfs1dUOSqDyqIqjtUY6Y-R_hjpj5H-GOmPkf4Y6Y-R_hjpj5H-"
        "GOmPkf4Y6Y-R_hjpj5H-GOmPkf4Y6Y-R_hjpj5H-GOmPkf4Y6Y-R_hjpj5H-GOmPkf4Y6Y-R_hjpj_"
        "9E9U9Uf6GKUa1RJahSVDeoNqi2qDJUt6iAbgx0Y6AbA90Y6MZANwa6MdCNgW6MpTIGvzH4jcFvDH5j8BuD3xjoxkA3AboJ0E2AbgJ0E6CbAN0E"
        "6CZANwG6CdBNgG4CdBOgmwDdBOgmQDcBugnQTYBuAnQTpD9B-hOkP0H6E6Q_QfoTpD9B-hOkP0H6E6Q_QfoTpD9B-hOkP0H6E6Q_QfoTpD9B-"
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
        "OfjNwW8OfnPwm4PfHPzmoLYAtQWoLUBtAWoLUFuA2gLUFqC2ALUFqC1AbQFqC1BbgNoC1BagtgC1BagtQG0BagtQW4DaAtQWoLYAtQWoLUBtAW"
        "oLUFuA2gLUFqC2ALUFqC1AbQFqC1BbgNoC1BagtgC1BagtQG0BagtQW4DaAtQWoLYAtQWoLUBtAWoLUFuA2gLUFqC2ALUFqC1AbQFqC1BbYNUt"
        "kf4S6S-R_hLpL5H5EpkvkfkSmS-R-RKZL5H5EpkvkfkSmS-R-RKZL5H5EpkvkfkSmS-R-RKZL5H5EpkvkfkSmS-R-RKZLzG-c1zjHNc4h-"
        "QCw73AcC8w3Au8SC4w3QGmO8B0B5juANMdYLoDTHeA6Q4w3QGmO8B0B5juAPkGyDdAvgHyDTDdAZIOkHSApAMkHSDpAAMKMKAASQdIOkDSAZIO"
        "kHSApAMkHSDpAEkHSDpA0gGSDpB0gIwCJB0g6QBJB5juANMdYLoDTHeA6Q4w3QGmO8B0B5juANMdYLoDTHeA6Q4w3QGmO8B0B5juANMdAGcAfg"
        "H4BeAXgF8AfiH4heAXgl8IfiH4heAXgl8IfiH4heAXgl8IfiH4heAXgl8IfhHiRYgXIV6EeBHiRYgXIV6EeBHiRYgXIV6EeBHiRYgXIV5UxsN8"
        "RJiPCPMRYT4izEeE-YgwHxHmI8J8RJiPCPMRYT4izEeE-YgwHxHmI8J8RJiPCPMRYT4izEeE-YgwHxHmI8J8RJiPCPMRYT4izEeE-"
        "YgwHxHmI8J8RJiPCPMRYT4izEeE-YgwHxHmI8J8RJiPCPMRYT4izEeE-YgwHxHmYwV-K_Bbgd8K_FbgtwK_"
        "FQitgGQFJCsgWQHECiBWALFC0iskvULSKyS9wuhXGP0KQ1thaCsMbYWhrTC0FYa2wtBWGNoKQ7sGq2uwugara7C6BqtrsLoGq2uwugara7C6Bq"
        "trXO0aXGIMN8aExpjQGBMaY_"
        "QxRh9j9DEmNMaExsgjRh4x8ogxoTEmNMaExpjQGLnFyC1GbjFyizHIGIOMMcgYg4wxyBiDjDHIGIOMMcgYg4wxyBiDjIEkxoTGmNAYExqXA8eE"
        "xpjQGBMag1oMajGoxaAWg1oMajGoxQAWYy5jzGWMuVxjLteYyzXmco25XGMu15jLNV4Ga7wM1ngZrPEyWONlsAa1NaitQW0NamtQW4PaGtTWoL"
        "YGtTWorUFtDWprUFuD2hrU1qC2BrU1qK1BbQ1qa1Bbg9oa1Nagtga1NaitQW0NamtQW4PaGtTWoLYGtTWorUFtDWprUFuD2hrU1qC2BrU1qK1B"
        "bQ1qa1Bbg9oa1NZYa2ugWwPdGujWQJcAXQJ0CdAlQJcAXQJ0CbaRBPwS8EvALwG_BPwS8EvALwG_BPwS8EvALwG_BPwS8EvALwG_"
        "BPwS8EvALwG_BPwS8EvALwG_BPwS8EvALwG_BPwS8EvALwG_BPwS8EvALwG_BPwS8EvALwG_BPwS8EvALwG_BPwS8EvALwG_BPwS8EvALwG_"
        "BPwS8EvALwG_BPxS8EvBLwW_FPxS8EvBLwW_FPxS8EvBLwW_FPxS8EvBLwW_FPxS8EvBLwW_FPxS8EvBLwW_FPxS8EvBLwW_FPxS8EvBLwW_"
        "FPxS8EvBLwW_FPxS8EvBLwW_FPxS8EvBLwW_FPxS8EvBLwW_FPxS8EvBLwW_FPxS8EvBLwW_FPxS8EvBLwW_FPxS8EvBLwW_"
        "FPw2iLdBvA3ibRBvg3gbxNsg3gbxNoi3QbwN4m0Qb4N4W0zAFhOwxQRsMQFbTMAWE7DFBGwxAVtMwBZ0t6C7Bd0t6G5Bdwu6W9Ddgu4WdLeguw"
        "XdLehuQXcLulvQ3YLuFnS3oLsF3S3obkF3C7pb0N2C7hZ0t6C7Bd0t6G5Bdwu6W9Ddgu4WNLagsQWNLWhsQWMLGlvQ2ILGFjS2oLEFjS1obEsa"
        "oLsF2AxcMnDJwCUDlwxcMnDJsOAyLLgMSDIgyYAkA5IMSDIgyYAkA5IMSDIgyYAkA5IM6WdIP0P6GdLPkH6G9DOknyH9DOlnyC1Dbhlyy5Bbht"
        "wy5JYhtwy5ZS_v_n1z_PcfL9N3_353_Afjxy85Smn--jtKab7_"
        "J0plHlBK82SBUplLlMo8R6nMC5TKDFAqM0SpzA1KZW5RSvPsBqU0p2WpzC8olXmHUplfUSrzHqUyH1Aq8wWlMg8olfkdpTSP70WPpTTPtyiVma"
        "FU5hNKaV78glKZryiVmaOU5vHu_Vgq81eUyhyhVOZvKJX5HqUyJyiV6aFUZoBSmVcolfkHSmX-E6Uy_"
        "0KpzASlMrcolXmHUplfUSrzBaUyDyiV-R2lNI-308dSmSOUyrxGqcw_"
        "UCrzL5TKXKNUZoJSmTcolfmKUpkWpTSPt8THUpkLlMpcolTmOUplXqBUZohSmSuUyrxEqcw_"
        "UCozRanMHUpl3qOU5uoDSmWWpTI9lMqco1TmBqUydyiVeY9SmY8olfmMUpkvKJV5QCnNyxFKZX5AqczfUSrzDKUyxyiVWZbK9FEqc4FSmecolX"
        "mFUpl_olTmHqUyH1FK8-o9SmWeolTmXyiVGaOU5uc_UUrzeGt5LJX5DaU0b_-BUpknKJV5igLz__"
        "6yQ6lMg1KaHy9QKjNEgfn39T92ZVU37suqbtiyqhtFWVWND6OyqhvPZVU1zv4sq7oRl1XdWJdV3UjKqm6kZVU3bsqqbmzKqm5sy6puZGVVN27L"
        "qm48lVXVGJuyqhqT87KqGxdlVTfCsqobUVnVjVVZ1Y3PZVU3rsuqakw_"
        "lVXdOCurujEuq7oxKau6sSirurEsq7pxXlZ146Ks6kZQVnUjLKu6EZVV3ViVVd24LKu6cVVWdeNzWdWN67KqG_uyqhvfyqpuPJdV1ZiVnx_"
        "Ui8L7tayqxtyUVdVY_"
        "l5WdeNjWdWNr2VVN3ZlVTf2ZVU3Hsuqbnwrq7rxVFZ147ms6sZLWdWNQ1lVjfPTsqobH8uqbnwqq7pxVlZ1Y1xWdWNWVnVjXVZ146as6sZ9WdW"
        "Nh7KqG3lZVY2LUVnVjd_Kqm68L6u64ZVV3fDLqm7My6puXJZV3bgqq7rxuazqxp9lVTf-"
        "Kqu6cVNWdeNrWdWNfVnVje9lVTVWn8qqbozLqm5MyqpuVFXd-KOs6safZVU3_llWdeOvsqobcVnVjXVZ1Y1NWdWNXVnVjX1ZofFvP377692_"
        "HS18_U9lVTf-c1nVjf9SVnXjv5ZV3fhvZVU3_ntZ1Y3_UVZ143-WVd34X2VVN_53WdWN_"
        "1NWdePv69PkWG5obGhsaWQ0bmnc0bin8UDjkcYTjWcaLzQONL7TyGkYGniBn6Zo0EhpbGhsaWQ0bml8oXFH4yuNexoPNPY0Hmk80Xim8ULjQOO"
        "VRk7D0igHf8PB3xDvDQd_wzHfcMw3HPMNxwxjR-OexgONPY1HGt9oPNP4TuOVRk7D0LA0yjFvSHXDgW2IbsPxbDiMDa--ofuWKW-"
        "Z8pYBtwy4ZaZbDnVLvFvi3XLwGWlkHEbGYWQcRsZhZAx4y2Hcctnckvwtx3PLKbjlwG45sFte9Jbkb3mtW5K_"
        "JfBbrpbb9tVfaBxofKeR0yjJf2E6X3iJL7zEFwb8wjhfGOeOeO-Y1x3zuuOY7xjwjgHvCOqOY77jJe445jte6ysDfmWcr3T_"
        "ytx31OzIcEeGOzLc0X3HYeza7s80chqWRrmi7jmV95zKe-Z-z0vck-E9A94znXumc090MF5plNd64CUeGPmBVB-Y4ANXywMzfWCmD7z6A8k_"
        "MOUHpvxAdA_M_YFT8MAxP3DMDxzzA8f8wDE_EBQMQ-Mti5LYnunsmc6eWeyZxZ5Z7Mlwz8HvOdQ9h7rnCPccz57XemSm33jRb7zWN17rG6_"
        "1jcS-kdg3XvRbO86exjONFxoHGjmNMvcnXv2ZV39mFs90f-bgn-n-"
        "Qi8YKY0bGhsaWxq3NL7QuKPxlcaOxj2NBxp7Go80nmm80DjQeKVhaFgaJfkDB3_gmA8c84HkDxzzgWM-"
        "cKgHDvXAoR441AOHeuBQDxzqgSM8cIQHwjxwYN85sO-8-ndG_s7I35nyd-b1ne6vdH9lXq-"
        "cglcGfCX5VwZ85eTmzD3nJQwztYxjmUXBaxUcYVGnM338f3_8mh7LP49lfSx3dbtwO_fH8texPHZ1Ph_LbS0q8Oin_GGo6mtefzX1V1t_"
        "LfAkodKVX_P6q6m_2vprgR8NrXTl17z-auqvtv4K3ada96nWfap1n2rdp1p3VuvOat1ZrTurdWe1blzrxrVuXOvGtW5c_4DX5B_1T3-"
        "VRk7D0LA0SnIj_qjYiD8uNuKPjI34Y2Mjin-j-DeKf6P4N4p_o_g9xe8pfk_xe4rfU3xK8SnFpxSfUnxK8UeKP1L8keKPFH-k-BPFnyj-"
        "RPEnij9RfEbxGcVnFJ9RfFaLPaLziM4jOo_oPKLziM4jOo_oPKLziM47ofiE4hOKTyg-oZjoPKLziM4jOo_ovA8Uf6D4A8UfKP5A8e8U_"
        "07x7xT_TvHvFHNSPE6Kx0nxOCkeJ8XjpHicFI-"
        "T4nFSPE6Kx0nxOCkeJ8XjpHhvkzKmeEzxmOIxxXyleBOKJxRPKJ5QPKF4SvGU4inFU4qnFM8onlE8o3hG8YzihOKE4oTihOKE4pTilOKU4pTil"
        "OIbim8ovqH4huIbijcUbyjeULyheENxRnFGcUZxRnFG8S3FtxTfUnxL8S3FXyj-QvEXir9Q_IXiO4rvKL6j-"
        "I7iO4p3FO8o3lG8o3hH8T3F9xTfU3xP8T3FDxQ_UPxA8QPFDxTvKd5TvKd4T_Ge4keKHyl-pPiR4keKv1H8jeJvFH-j-BvFTxQ_"
        "UfxE8RPFTxQ_U_xM8TPFzxQ_U_xC8QvFLxS_UPxCcU5xTnFOcU5xXov9X2txaeQ0DA1LoxRzf_a5P_vcn33uzz73Z5_7s8_92ef-7HN_9rk_-"
        "zxhfZ6wPk9YnyeszxPW52buczP3uZn73Mx9buY-N3Ofm7nPzdznZu5zM_e5I_"
        "nckXzuSD53JJ87ku9R7FHsUexR7FG8pXhL8ZbiLcXbWrzkEl1yiS65RJdcoksu0SWX6JJLdMkluuQSXXKJLr9T_J3i7xR_p_"
        "h7LT6f1eLSyGkYGpZGKV5SvKR4SfGS4iXF5xSfU3xO8TnF57U45NoIuTZCro2QayPk2gi5NkKujZBrI-"
        "TaCLk2Qq6NkGsj5NoIuTZCro2QJ2zIEzbkCRvyhA15woY8YUOesCFP2JAnbMgTNuQJG_KEDXnChjxhQ56wIU_"
        "YkCdsyBM25Akb8oQNuZ5DrueQ6znkeg65nqO_"
        "anFp5DQMDUujFHOJRlyiEZdoxCUacYlGXKIRl2jEJRpxiUZcohF30Yi7aMRdNOIuGnEXXf1Si0sjp2FoWBqlmJOy4qSsOCkrTsqKkxJTHFMcUx"
        "xTHL-"
        "JyTkm55icY3KOyTkOKA4oDigOKA4oDikOKQ4pDikOKY4ojiiOKI4ojiheUbyieEXxiuIVxZcUX1J8SfElxZcUX1F8RfEVxVcUX1H8meLPFH-m-"
        "DPFnym-pvia4muKrym-ppg3gTFvAmPeBMa8CYx5ExjvKN5RvKN4R_GOYt7JxLyTiXknE_"
        "NOJuadTMw7mZh3MjHvZGLeycS8k4l5JxPzTibmnUzMO5mYdzIxX4MxX4MxX4MxX4MxX4MxX4MxX4MxX4MxX4MxX4MxX4MxX4MxX4MxX4MxX4Mx"
        "72Ri3snEvJOJeScT804m5p1MzDuZmHcyMe9kYt7JxAeKDxQfKD5QfKDYUGwoNhQbig3FlmJLsaXYUmxr8ZoJrpngmgmumeCaCSbknJBzQs4JOS"
        "fknJBzQs4JOSfknJBzQnQJ0SVElxBdQnQJ0SVElxBdQnQJ0SWvFL9S_"
        "ErxK8WvtTjlaZXytEp5WqU8rVKeVilPq5SnVcrTKuVplfK0SrmLptxFU-6iKXfRlLtoyruvlHdfKe--"
        "Ut59pbz7Sn2KfYp9in2KfYrnFM8pnlM8p3hO8YLiBcULihcULyjmPVLKe6SU90gp75FS3iOlvEdKeY-U8h4p5T1Synuk9ILiC4ovKL6g-"
        "IJiHkApD6CUB1DKAyjlAZTyAEp5AKU8gFIeQCkPoJQHUMoDKOUBlPIASnkApTyAUh5AKQ-"
        "glAdQygMo5QGU8gBKeQClPIBSHkApD6CUB1DKAyjlAZTyAMr4PiXj-5SM71Myvk_J-D4l4yO4jI_gMj6Cy_"
        "gILuMjuIw3rhlvXDPeuGa8cc1445rtKN5RvKN4R_GOYh5AGQ-"
        "gjAdQxgMo4wGU8QDKeABlPIAyHkAZD6CMB1DGAyjjAZTxAMp4AGU8gDIeQBkPoIwHUMYDKOPGmHFjzLgxZtwYM26MGTfGjBtjxo0x48aYcWPMu"
        "D9n3J8z7s8Z9-eM-3PGXTTjLppxF824i2Zvb6Vvfvwy_nEVH-u_61_0rL-DHzpQ3yy_43_qV6u-v-tfJKWtv5W27Y5ureqI2x--4yr9F-u_"
        "5kaLN73X3PT6d1xz03FNzKCYmL9bz-"
        "Re3Qnq6Gx68iE3t7PpMUNubmfTY4fc3M6mpxhyczvlQhsgM6RRgj5OQxol6KM2pFGCPoZDGiXoI9qrwTuWnkxll-nvsv1db9fKe9d1PrSulVs-"
        "5OZ2doy-y83t7Misy83t7M-6e1233TrXrOM9pOlfswNBOnF1rtmBIJ3wOtfsQJBOlJ1rdiCI0Mhl-NrfZfq7bH_X27VM77o2Q-taueVDbm5nx-"
        "i73NzOjsy63NzO_qy713XbrXPNOt5Dmv41OxCkE1fnmh0I0gmvc80OBOlE2blmB4IITc-61l15f5ft73q7lu1d13ZoXSu3fMjN7eyg3-"
        "XmdnZk1uXmdvZn3b2u226da9bxHtL0r9mBIJ24OtfsQJBOeJ1rdiBIJ8rONTsQRGh61rXuyvu7TH_"
        "X27WK3nVdDK1r5ZYPubmdHUPscnM7O3h3ubmd_Vl3r-u2W-eadbyHNP1rdiBIJ67ONTsQpBNe55odCNKJsnPNDgQRmp51rbvy_"
        "i7T31X9pIWY99Z35Jt5Idt0yTauTLyv3_Sk3OfUdQnV93f9rrjjXXX77XTH--iON9D1tzri9ofvuEr_xbqu-Xffm_"
        "ZWj95UOt3yITe301kffW5up7t2etzcTmd597m5nTW2ISRdnU1PL5KuzqanF0lXZ9PTi6Srs-npRdLV2b2HdngPabr30J8E6cSl9tCfBOmEp_"
        "bQnwTpRKn20J8EGQQ7tOSGNErwM7C967At-BnY3lXZFvwMbO8abQt-BlZpOk6p7i7T32X7u3qupajKrp5rKQKy6-1a3Q-jWj3d-3X3w6g-"
        "N7ezg1Tvw6hONzvk5nZ2EO59GKXcNkNIujo7Jq3LrRvJZghJV2fHZHe5dSPpWgjd-_Xg87kO7yFN_178s-dzQ0H6qA1p-"
        "vfinz2fGwrSR3RI078X9wTpXXmde_FAkEGwQ8txSNO_Fw8EGQQ7tFR7NR1vOLq7TH-X7e_"
        "quZaamp79Wnf1XEvk1f2QtdXTvV93P2Ttc3M7O0j1PmTtdLNDbm5nB-Heh6zKbTOEpKuz46XQ5daNZDOEpKuzY7K73LqRdC2E7v168Llzh_"
        "eQpn8v_tlz56EgfdSGNP178c-eOw8F6SM6pOnfi3uC9K68zr14IMgg2KHlOKTp34sHggyCHVqqvZqe_brngX53l-3v6rmWmpqeW2_"
        "d1XMtkVf3hwetnu79uvvDgz43t7PjJdT74UGnmx1yczs7CPd-eKDcNkNIujo7ZqbLrRvJZghJV2fHZHe5dSPpWgjd-_Xg5ykd3kOa_r34Z5-"
        "nDAXpozak6d-Lf_Z5ylCQPqJDmv69uCdI78rr3IsHggyCHVqOQ5r-vXggyCDYoaXaq-nZr3s-qOruMv1dPddSU9OzX-uunmuJvLo_FGv1dO_"
        "X3R-K9bm5nR04ej8U63SzQ25uZwfh3g_FlNtmCElXZ8fMdLl1I9kMIenq7FjuXW7dSLoWQvd-3feZWB-ZIU3_"
        "XjwQpBNX5148EKQTXudePBCkE2XnXjwQZBDs0JIb0vTvxQNBBsEOLcchTf9ePBBkEOzQUu3V9OzXuivv7zL9Xba7S01Nz36tu3quRTip1_"
        "529V9rl78xonnJXyDpft7fqVEC8y8EcTVKYP-FIK5GCYp_IYir-TmMfgqD6ffnPZhwf6aDKerO-m-stdpvy6Jefj1deX-X6e-y_V1vw-"
        "p4jOn8QlP_vLma_iU2EMTV9C-xgSCupn-JDQTpZDIIo5_CYPr9eQ8m3J_pYIq6s70MxUNcnU_PMtRdpr_L9ne9Davj6YwcTPfTmU5N_"
        "3Y5EMTV9C-xgSCupn-JDQTpZDIIo5_CYPr9eQ8m3J_pYIq6s70MxbMpnU_PMtRdpr_L9ne9DavjTaccTPebzk5N_xIbCOJq-rfLgSCupn-"
        "JDQTpZDIIo5_CYPr9eQ8m3J_pYIq6s70MxVtunU_PMtRdpr_L9ne9DavjXloOpvteulPTv8QGgria_iU2EMTV9G-XA0Fczc9h9FMYTL8_78GE-"
        "zMdTFF3tpeheCeh8-lZhrrL9HfZ_q7m93rL7zW_rOt8z3R8z3Z87y1e3nzvteN7puN7tuN7b_FMRzzTMWbTEc90xLMd8WxHPNsxZtsRr-"
        "iIV3TEKzriFXLM1e_vln8Lj-Z_VP9Vf9U6b5ntjssPLbPVUf8Z7dou_5I07byxyz_jTDtu2a1fGy7_"
        "fDHtL43tv7bsVsxF67dty79zWdvlHxSk7Td2-"
        "WezaLfiJPL3M2s0TZPJvn3n3Gm6ghpV03QEb8je2jW2pu38NmaNr2nHTvtGtmuUTfuLbPuvTtu53iJ12reyXSNu2r5s16ibthO_"
        "QZ5L5LlCnkvkuUKeS-"
        "S5Qp47yHMHee4gzx3kuYM8d5DnDvLcQZ47yHMHee4gzx3kuYM8d5DnDvLcQZ47yI1EbhRyI5EbhdxI5EYhNw5y4yA3DnLjIDcOcuMgNw5y4yA3"
        "DnLjIDcOcuMgNw5y4yA3DnLjIDcOciuRW4XcSuRWIbcSuVXIrYPcOsitg9w6yK2D3DrIrYPcOsitg9w6yK2D3DrIrYPcOsitg9w6yK2DvJDIC4"
        "W8kMgLhbyQyAuFvHCQFw7ywkFeOMgLB3nhIC8c5IWDvHCQFw7ywkFeOMgLB3nhIC8c5IWDvGgh3zS4NwL1psG8EYg3Dd6NQLtpYd20kG5aODct"
        "lJsWxk0L4aaFb9NCt2lh27SQbVq4Ni1UmxamTQvRpoVn00KzaWORdxWtJpOVdxWtpiuoUam7Cn7nDZlzV9FqOz9WX-"
        "Nz7ipa7RvZrlE6dxVN23912s71FqnTvpXtGrFzV9G0a9TOXUWrLX8ut0GeK-"
        "S5RJ4r5LlEnivkuYM8d5DnDvLcQZ47yHMHee4gzx3kuYM8d5DnDvLcQZ47yHMHee4gzx3kuYPcSORGITcSuVHIjURuFHLjIDcOcuMgNw5y4yA3"
        "DnLjIDcOcuMgNw5y4yA3DnLjIDcOcuMgNw5y4yC3ErlVyK1EbhVyK5Fbhdw6yK2D3DrIrYPcOsitg9w6yK2D3DrIrYPcOsitg9w6yK2D3DrIrY"
        "PcOsgLibxQyAuJvFDIC4m8UMgLB3nhIC8c5IWDvHCQFw7ywkFeOMgLB3nhIC8c5IWDvHCQFw7ywkFeOMhbdxXlJ94VbpoVqLp13jLbHSVemq2O"
        "Gmttl0hp541doqQdt-"
        "ybxi7x0f7S2P5ry27FLHHRvm3sEhNtv7FLPLRbcRL5mXONxr2raH3n3Gm6ghqVe1fx9p03ZPKuot12PlKu8cm7inb7RrZrlPKuotX2nU-"
        "zfed6NVp5V9Fq14jlXUWrXaOWdxXttvx8tUGeK-"
        "S5RJ4r5LlEnivkuYM8d5DnDvLcQZ47yHMHee4gzx3kuYM8d5DnDvLcQZ47yHMHee4gzx3kuYPcSORGITcSuVHIjURuFHLjIDcOcuMgNw5y4yA3"
        "DnLjIDcOcuMgNw5y4yA3DnLjIDcOcuMgNw5y4yC3ErlVyK1EbhVyK5Fbhdw6yK2D3DrIrYPcOsitg9w6yK2D3DrIrYPcOsitg9w6yK2D3DrIrY"
        "PcOsgLibxQyAuJvFDIC4m8UMgLB3nhIC8c5IWDvHCQFw7ywkFeOMgLB3nhIC8c5IWDvHCQFw7ywkFeOMjf7irqP-EFQPjT6-0WcLW_"
        "MZKtpjsX3rnrnQvv3PU2wtu43kZ4G9fbCm_relvhbV3vQngXrnchvAvh3XhWHz02jo0Te-o_"
        "XDz9rXZqTNExapnsaE2ObCrByGm2BLmMkKsIuYyQqwhGRjAqgpERjIpgZQSrIlgZwaoIhYxQqAiFjFCICO8b7_"
        "fC833j9V56SPatphKMnGZLkMsIuYqQywi5imBkBKMiGBnBqAhWRrAqgpURrIpQyAiFilDICJL9x8b7o_"
        "D82Hh9lB6SfaupBCOn2RLkMkKuIuQyQq4iGBnBqAhGRjAqgpURrIpgZQSrIhQyQqEiFDKCZP9Jkmw1lWDkNFuCXEbIVYRcRshVBCMjGBXByAhG"
        "RbAyglURrIxgVYRCRihUhEJGcEi2vMWe_9Y3En30O5Mz0GoqwchptgS5jJCrCLmMkKsIRkYwKoKREYyKYGUEqyJYGcGqCIWMUKgIhYwgZ-"
        "Cs5e3OwFnL0ZkB7-7Nj6boGLVMdog5azeVYOQ0W4JcRshVhFxGyFUEIyMYFcHICEZFsDKCVRGsjGBVhEJGKFSEQkYQc-Z9bby_Cs-"
        "vjddX4bFrPHbCY9d47KSHnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq2dnK2dmq37xvteeN43X"
        "vfSQ7K_V-zvJft7xf5esr9X7O8l-3vF_l6yv1fs7yX7e8X-XrK_V-zvJft7xf5esr9X7O8l-3vFft9474XnvvHaSw_Jfq_Y7yX7vWK_l-"
        "z3iv1est8r9nvJfq_Y7yX7vWK_l-z3iv1est8r9nvJfq_Y7yX7vWL_3Hg_C8_nxutZekj2z4r9s2T_rNg_S_bPiv2zZP-s2D9L9s-K_bNk_"
        "6zYP0v2z4r9s2T_rNg_S_bPiv2zZP_ssvd_"
        "ffOmKTpGLZMdgn27qQQjp9kS5DJCriLkMkKuIhgZwagIRkYwKoKVEayKYGUEqyIUMkKhIhQygmQ_arxHwnPUeI2kh2Q_"
        "UuxHkv1IsR9J9iPFfiTZjxT7kWQ_UuxHkv1IsR9J9iPFfiTZjxT7kWQ_UuxHkv1IsT9pvE-E50njdSI9JPsTxf5Esj9R7E8k-xPF_"
        "kSyP1HsTyT7E8X-RLI_UexPJPsTxf5Esj9R7E8k-xPF_kSyP1HsTxvvU-F52nidSg_J_lSxP5XsTxX7U8n-VLE_lexPFftTyf5UsT-V7E8V-"
        "1PJ_lSxP5XsTxX7U8n-VLE_lexPXfbVXzN-i9BuKsHIabYEuYyQqwi5jJCrCEZGMCqCkRGMimBlBKsiWBnBqgiFjFCoCIWM4JBseTvvh-u-"
        "keir_cJ_"
        "vPnRFB2jlskOMWftphKMnGZLkMsIuYqQywi5imBkBKMiGBnBqAhWRrAqgpURrIpQyAiFilDICGLOwmbXD8WuHza7fngiPSR7teuHctcP1a4fyl"
        "0_VLt-KHf9UO36odz1Q7Xrh3LXD9WuH8pdP1S7fih3_VDt-qHc9UO164dy1w_Vrh82u34odv2w2fXDU-kh2atdP5S7fqh2_VDu-"
        "qHa9UO564dq1w_lrh-qXT-Uu36odv1Q7vqh2vVDueuHatcP5a4fql0_lLt-qHb9sHl6SlN0jFomOyR79eQ6lE-uQ_"
        "XkOpRPrkP15DqUT65D9eQ6lE-uQ_XkOpRPrkP15DqUT65D9eQ6lE-uQ_XkOpRPrkP15DqUT65D9eQ6HDfeY-E5brzG0kOyHyv2Y8l-"
        "rNiPJfuxYj-W7MeK_ViyHyv2Y8l-rNiPJfuxYj-W7MeK_ViyHyv2Y8l-rNhPGu-J8Jw0XhPpIdlPFPuJZD9R7CeS_USxn0j2E8V-"
        "ItlPFPuJZD9R7CeS_USxn0j2E8V-ItlPFPuJZD9x2Ud_"
        "vXnTFB2jlskOwb7dVIKR02wJchkhVxFyGSFXEYyMYFQEIyMYFcHKCFZFsDKCVREKGaFQEQoZQbJ_bLwfhedj4_UoPST7R8X-UbJ_"
        "VOwfJftHxf5Rsn9U7B8l-0fF_lGyf1TsHyX7R8X-UbJ_VOwfJftHxf5Rsn9U7Jv3BTRFx6hlskOyV-_"
        "JWt8ZOc2WIJcRchUhlxFyFcHICEZFMDKCURGsjGBVBCsjWBWhkBEKFaGQEST7p8b7SXg-NV5P0kOyf1LsnyT7J8X-SbJ_"
        "UuyfJPsnxf5Jsn9S7J8k-yfF_kmyf1LsnyT7J8X-SbJ_"
        "UuyfJPsnl32cvHnTFB2jlskOwb7dVIKR02wJchkhVxFyGSFXEYyMYFQEIyMYFcHKCFZFsDKCVREKGaFQEQoZQbJv9pxY7Dlxs-fE36SHZK_"
        "2nFjuObHac2K558Rqz4nlnhOrPSeWe06s9pxY7jmx2nNiuefEas-J5Z4Tqz0nlntOrPacWO45sdpz4ubTq1h8ehU3n17Fz9JDslefXrW-"
        "M3KaLUEuI-QqQi4j5CqCkRGMimBkBKMiWBnBqghWRrAqQiEjFCpCISNI9i-N94vwfGm8XqSHZP-i2L9I9i-K_Ytk_6LYv0j2L4r9i2T_oti_"
        "SPYviv2LZP-i2L9I9i-K_Ytk_6LYv0j2L4r9ofE-CM9D43WQHpL9QbE_SPYHxf4g2R8U-4Nkf1DsD5L9QbE_SPYHxf4g2R8U-"
        "4Nkf1DsD5L9QbE_"
        "SPYHxb7FQObfyl3m3cpZ5tvKVeZp5GwZNVtGzpZRs2XkbBk1W0bOllGzZeRsGTVbRs6WUbNl5GwZNVtGzpZRs2XkbBk1W0bOllGz1bq-"
        "vHbruvKaVrK3ir2V7K1ibyV7q9hbyd4q9layt4q9leytYm8le6vYW8neKvZWsreKvZXsrcs-aU6IRJwQSXNCJC_SQ7BP1AmRyBMiUSdEIk-"
        "IRJ0QiTwhEnVCJPKESNQJkcgTIlEnRCJPiESdEIk8IRJ1QiTyhEjUCZHIEyJRJ0TSnBCJOCGS5oRIDtJDslcnRCJPiESdEIk8IRJ1QiTyhEjUC"
        "ZHIEyJRJ0QiT4hEnRCJPCESdUIk8oRI1AmRyBMiUSdEIk-"
        "IRJ0QafPUmaboGLVMdgj27aYSjJxmS5DLCLmKkMsIuYpgZASjIhgZwagIVkawKoKVEayKUMgIhYpQyAiSffPUmaboGLVMdkj26qlz6zsjp9kS5"
        "DJCriLkMkKuIhgZwagIRkYwKoKVEayKYGUEqyIUMkKhIhQygmQ_bbzFR_J1a9Qy2SHZTxX7qWQ_Veynkv1UsZ9K9lPFfirZTxX7qWQ_"
        "Veynkv1UsZ9K9lPFfirZTxX7qWQ_VewXjfdCeC4ar4X0kOwXiv1Csl8o9gvJfqHYLyT7hWK_kOwXiv1Csl8o9gvJfqHYLyT7hWK_"
        "kOwXiv1Csl8o9ueN97nwPG-8zqWHZH-u2J9L9ueK_blkf67Yn0v254r9uWR_rtifS_bniv25ZH-u2J9L9ueK_"
        "blkf67Yn0v254r9ReN9ITwvGq8L6SHZXyj2F5L9hWJ_IdlfKPYXkv2FYn8h2V8o9heS_YVifyHZXyj2F5L9hWJ_"
        "IdlfKPYXkv2FYh803oHwDBqvQHpI9oFiH0j2gWIfSPaBYh9I9oFiH0j2gWIfSPaBYh9I9oFiH0j2gWIfSPaBYh9I9oFiHzbeofAMG69Qekj2oW"
        "IfSvahYh9K9qFiH0r2oWIfSvahYh9K9qFiH0r2oWIfSvahYh9K9qFiH0r2oWIfNd6R8Iwar0h6SPaRYh9J9pFiH0n2kWIfSfaRYh9J9pFiH0n2"
        "kWIfSfaRYh9J9pFiH0n2kWIfSfaRYr9qvFfCc9V4raSHZL9S7FeS_UqxX0n2K8V-JdmvFPuVZL9S7FeS_UqxX0n2K8V-JdmvFPuVZL9S7FeS_"
        "Uqxv2y8L4XnZeN1KT0k-0vF_lKyv1TsLyX7S8X-UrK_VOwvJftLxf5Ssr9U7C8l-0vF_lKyv1TsLyX7S8X-UrK_VOyvGu8r4XnVeF1JD8n-"
        "SrG_kuyvFPsryf5Ksb-S7K8U-yvJ_kqxv5LsrxT7K8n-SrG_kuyvFPsryf5Ksb-S7K8U-8-N92fh-bnx-iw8rhuPa-"
        "Fx3Xhctz34X9DvWvZ9y35o2c7_EV_7NO17p_3gtJ3_8Lzxzx3_3PHPHX_j-BvH3zj-xvG3jr91_K3jbx3_wvEvHP_C8S9a_"
        "puW76blt2n5bNp6h_fG4b1xeG8c3huH98bhvXF4bxzeG4f3xuG9cXhvHN4bh_fG4b1xeG8c3huH98bhvXF4byRv_reFu5Z937IfWrbz_"
        "wrWPpJ3u_3gtJ3_JK_xzx3_3PHPHX_j-BvH3zj-xvG3jr91_K3jbx3_wvEvHP_C8X_jzT8Ys1i_a_2FmKpVmptDu-"
        "PYOnqXv2EdpaVZ9Rxb1e5U9v19ffah1clm-aOMwR8ts_KZfUDrdgVzWQ3sO-yk-XZ52ly9b5n1Zvhrqb8o_6vSX3m5i5uqN7grmz56g2-"
        "Vjf9uLHiqRiXsetut_-PO9-_-P_-VKLY");
    static string all_emojis_str = gzdecode(base64url_decode(packed_emojis).ok()).as_slice().str();
    constexpr size_t EMOJI_COUNT = 4713;
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
