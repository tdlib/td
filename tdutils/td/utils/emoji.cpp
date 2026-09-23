//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/emoji.h"

#include "td/utils/base64.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/Gzip.h"

namespace td {

static constexpr size_t MAX_EMOJI_LENGTH = 28;

static bool is_emoji_element(Slice str) {
  static const FlatHashSet<Slice, SliceHash> emojis = [max_emoji_length = MAX_EMOJI_LENGTH] {
#if TD_HAVE_ZLIB
    Slice packed_emojis(
        "eJxtmlly40iWRbdCs_rrr56H3WXGPBCDOwARgxSqrHTARTglUaQUEjWFWS0FtYAus95A-2HipqW19Yc7Hy0uHe-"
        "e5xMZmpbtYlp2iylbxzbGFmLbxLaNbb-YVi-xvS6muoytWkzNp9g-x_Yltq-xLWNLYktjy2LLYzOx2diK2H4spja-tr_EFsdv4_"
        "htHLONY3bvYnsfW_xsF8fv4r93u9jic7u72B5ji9rTqDmNzzuNzzu9WExn8Zln8TNncdyz-LmzmNfZajF9i6_fTmIjjvl_i8_eRG-"
        "b08V0GfO6rBZ_P__pXezeZrH7kNIZOktX0JV06D6c0K3oarqGro3dp5_pjtE13Y5uT3dLd0f3ne6B7kD3GLvPOR1PW_5ExyjLN3Rv6chq-"
        "Z7uA91Huk90n-m-0H2lW9IldGS_xMeSkZfHkfGxxMcSH0t8LPGxxMcSH0t8LMk-Vj12p3RndN_ozun-THdJd0WHy-WW7oYOv0v8LvG7xO_"
        "yng6_S_wu8bt8onume6F7pfsRuwT7CfYT7CfYT7CfYD_BfoL9BPsJ9hPsJ9hPsJ9gP8F-gv0E-wn2E-wn2E-wn2A_wX6C_"
        "QT7CfYT7CfYT7CfYD_BfoL9BPvJL3R_"
        "ofuVztH1dAOdp7ugW9ONdIFuQwe6BHQJ6BLQJaBLQJeALgFdArqEqZLAL4FfAr8Efgn8EvgloEtAl4IuBV0KuhR0KehS0KWgS0GXgi4FXQq6FH"
        "Qp6FLQpaBLQZeCLgVdCroUdCn2U-yn2E-xn2I_xX6K_RT7KfZT7KfYT7GfYj_Ffor9FPsp9lPsp9hPsZ9iP8V-iv0U-yn2U-yn2E-xn2I_"
        "ZfqkMEhhkMEgg0EGgwwGGQwyGGQwyGCQwSCDQQaDDL8ZfjP8ZvjN8JvhN8Nvht8Mvxl-M_xm-M3wm-E3w2-G3wy_GX4z_"
        "Gb4zfCb4TfDYIajDEcZjjIcZTjKcJThKMNRjqMcRzmOchzlOMpxlOMox1GOoxxHOY5yqppT1Zyq5lQ1x2WOyxyXOS5zXOYsiJwFkbMgchZEzoL"
        "IWRA5CyJnQeQsiJwFkbMgchZEDqEcQjmEcgjlEMohlEMoh1AOoRxCOYRyCOUQyiGUQyiHUA6hnBmRMyNyZkQOsJwZkTMjcmZEDr8cfjn8cvjl8"
        "Mvhl8PPgM5AzUDNQM1AzUDNQM1AzUDNQM1AzUDNQM1AzUDNQM1AzUDNQM1AzUDNQM1AzUDNQM1AzUDNQM1AzUDNQM1AzUDNQM1AzUDNQM1AzUD"
        "NQM1AzUDNQM1AzUDNQM1AzUDNQM1AzUDNQM1AzUDNQM1AzUDNQM1AzUDNQM0cqTHrLOgss87Cz8LPws_Cz8LPws_Cz8LPws_Cz8LPws_"
        "Cz8LPws_Cz8LPws_Cz8LPws_Cz8LPws_Cz8LPws_Cz8LPws_Cz8LPws_Cz8LPws_Cz8LPws_Cz8LPws_Cz8LPws_Cz8LPws_Cz8LPws_"
        "Cz8LPws_Cz8LPws_Cz8LPws_Cz8KvgF8BvwJ-BfwK-BXwK-BXwK-AXwG_An4F_Ar4FfAr4FfAr4BfAb8CfgX8CvgV8CvgV8CvgF8BvwJ-BfwK-"
        "BXwK-BXwK-AXwG_An4F_Ar4FfAr4FfAr4BfAb8CfgX8CvgV8CvgV8CvgF8BvwJ-BfwK-BXwK-BXwK-AXwG_An4F_"
        "Ar4FVAroVZCrYRaCbUSaiXUSqiVUCuhVkKthFoJtRJqJdRKqJVQK6FWQq2EWgm1Emol1EqolVAroVZCrYRaCbUSaiXUSqiVUCuhVkKthFoJtRJ"
        "qJdRKqJVQK6FWQq2EWgm1Emol1EqolVAroVZCrYRaCbUSaiXUSqiVUCuhVkKthFoJtRJqJbOuwn6F_"
        "Qr7FfYrnFc4r3Be4bzCeYXzCucVziucVzivcF7hvMJ5hfMK5xXOK5xXOK9wXuG8wnmF8wrnFc4r8jvhGSc84wTJinRXpLsi3RWLZEW5a8pdU-"
        "6acteUu6bcNeWuKXdNuWvKXVPumnLX-K3xW-"
        "O3xm9NuWtM15iuMV1jusZ0TUI1CdWYrjFdY7rGdI3pGtM1pmtM15iuMV1jusZ0jekaRzWma0zXmK4pd025a8pdU-"
        "6acteUu6bcNeWuKXdNuWvKXVPumnLXlLum3DXlril3TblrcNbwq-FXw6-GXw2_Bn4N_"
        "Br4NfBr4NfAr4FfA78Gfg38Gvg18Gvg18CvgV8Dv5bxWsZrGa9lvJbxWsZrGa9lvJbxWsZrGa9lvJbxWsZrGa89jkc9WurRUo-"
        "WerTUo6UeLfVoqUdLPVrq0VKPlnq01KOlHi31aKlHSz1a6tFSj5Z6tNSjpR4t9WipR0s9WurRUo-"
        "WerTUo6UeLfVoqUdLPVrq0VKPlnq01KOlHi31aKlHSz1a6tFSj5Z6tNSjpR4t9WipRwe_Dn4d_"
        "Dr4dfDr4NdBqANJB5IOJB0gOkB0gOgA0QGiw36H_"
        "Q77HfY7fHT46EiyI8mOJDuS7EiyI8mOJDuS7EjyHGrnUDuH2jnUzqF2DrVzqJ1D7Rxq51A7h9o5TzuHkCNxR2kdpXWU1uHD4cPhw1FaR2kdjhy"
        "OHI4cjhyOHKV1lNbhzeHN4c3hzZGkI0lHko4kHUk6knQk6UjSkaQjSUeSjiQdSByldZTWUVp3TJzSOkrrKK2DmoOag5qDmoOag5qDmgOYo6qOq"
        "jqq2lPVnqr2VLWnqj1V7alqz4LoWRA9C6JnQfQsiB5qPdR6qPVQ66HWQ62HWg-"
        "1Hmo91Hqo9VDrodZDrYdaD7Ueaj3Ueqj1UOuh1kOth1oPtR5qPdR6qPVQ66HWQ62HWg-"
        "1Hmo91Hqo9VDrodZDrYdaD7Ueaj3Ueqj1UOuh1kOth1rPXOtB14OuB10PugF0A-gG0A2gG0A3gG5gQxngN8BvgN8AvwF-A_wG-A3wG-A3wG-"
        "A3wC_AX4D_Ab4DfAb4DfAb4DfAL8BfgP8BvgN8BvgN8BvgN8AvwF-A_wG-A3wG-A3wG-A3wC_AX4D_"
        "Ab4DfAb4DfAb4DfAL8BfgP8BvgN8BvgN8BvgN8AvwF-A_wG-A3wG-Dn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-"
        "Hn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-Hn4efh5-"
        "Hn4efh5-"
        "Hn4bdmvDXjrRlvzXhrxlsz3prx1oy3Zrw1460Zb814a8YbKcBIAUYKMFKAkQKMFGCkACMFGCnASAFGCjBSgJECjBRgpAAjBRgpwEgBRgowUoCR"
        "AowUYKQAIwUYKcBIAUYKMFKAkQKMFGCkACMFGCnASAFGCjBSgJECjBRgpAAjBRgpwEgBRgowUoCRAowUYKQAIwUYATYCbATYCLARYCPARoCNAB"
        "sBNgJsBNgIsPEIjAKMFGCkAAF-AX4BfgF-AX4BfgF-"
        "AXQBVgFWAVYBVgFWAVYBVgFWAVYBVgFWAVYBVgFWAVYBVgFWAVYBTAFMAUwBTAFMAUwBTAFMAUwBTAFMAUwBTAEuAS4BLgEuAS4BLgEuAS4BLg"
        "EuAS7hcfHX9eKvV4vpp-fF9PPnxfTuL7EdFtOHMrYqtpPYVrHVsTWxrWMbF9PyYjFlWWyXscXPZ9ex3cS2i-"
        "0htjhG9riY4hfL6STqT0Js3xfT6qfYnmKLz4s36ylerKd4r57itXqKt-"
        "op3pOneDme4u13ihfdKd5up3ivneK1dop32SneXad4V53iVXWKN9ApXkCneP-c4i1zivfLqTmPLX6mifqmjy1-"
        "pom5NvGZzetiine8Kd7upni5m-LdbopXuyle6qZ4nZvibW6Kt7cpXs-meOua4qVr6j7FFn3G29DUFbFF_138t45_u43tPraYR7zTTKfx-"
        "adRfxo5ni5jS2KLnz2NzzyNzzyNzzuNnk5_iW0fW_z8WfR89jG2mO-ZW0zf4r_FK8AUbwDT5n1sH2L7uPjbT9vYXhZ_-7KKjWn0nmK_"
        "p9jvmcjvmcifmL6fjj9kM42WTJ4lk2fJ5FkyeZZMniWTZ8nkWTJ5lqyx5fFH3OPvlEzalEmb8qCU-"
        "ZoyX1NWdMpUzdg6MpZDxnLIWA4Zsz5j1mcMkDFAxqzPGCVjlIxRMmZ9xqzPGC87jsdUzZil2fFXM8yY4-8Sx6-"
        "ObFkVW1bFNK9wXvGJisld8bGK7Cs-WzG5Kyb3CXveCR87Id0T0j0h3RPW7AlcTqBxAsQTVszJ8esfEFfsASv2gBVre8XaXrG2V2S_IvsV2a_"
        "gvGKlrhhqRX4rUlsdr7c8t-ORHYQ6NoyOVd7xsY5V3vHZjkJ1JNRRmQ6D3X7xp-ntr4s__fdDxus_HLv5zT8eu_nNPx27-c0_H7v5zb8cu_"
        "nNvx67-c2_Hbv5zb8fu_nNfxy7-c1_Hrv5zX8du_nN388_"
        "DrFdKFgrGBUEBRsFVwpuFOwU3Cr4ruBewYOCg4JHBc8KXhQwcT563ijwCtYKRgVBwUbBpYIrBdcKbhTsFOwV3Cr4ruBewYOCg4InBc8KXhUck7"
        "9Q8hfCe6HkL5TzhXK-UM4Xyplgq-BGwU7BXsGtgjsF3xXcK3hU8KTgWcGLglcFx-TXwrtWhmsxXCuxtfJZK421Pj7K-"
        "yjvowYcNeAoy6NSHcV5FOdRyQdhCUojKI2gNILSCBpwozQ2mj8blWCjfDaqxUaJbZTYRg_dqAQbPWujEmxEfiPymz8-_UHBQcGjgmcFR_"
        "KXsnOpR1zqEZca8FLjXGqcK-G9kq8r-bpSzlca8EoDXgnUlXK-0iOulPOVnnWtAa81zrU-fi3vW2m2YrgVw60YbvXxrdLY_"
        "vHj9wqeFbwqOM6oG5XyRqW8kfcbPeJGDG804I3s3MjOjdARPCk4PmunR-w08k5UdzK402zZyelOTnd6-k7kd7K8k-"
        "Wd0O3kfacS7JTzTjnvlPNOOe-U806gCF4U_"
        "O7iSGwvO3vZ2cvFXi72crEXw72S3yvVvVLdK8O98tnrWbdyeqeH3ulZd3rWnZ51J2J3Inanh979cZy9gnsFDwoOCp4VHL1_19Pv9fR7ubjXx--"
        "V_L0-_qBPEXgFFwrWCkYFGwWXCq4UXCvYKrhRsFOwV3Cr4F7Bg4KDgicFLwpeFRzJH5T8QTkflPNB5A_K-"
        "aCcD0r1oFQPSvWgVA9K9aBUD0r1oAwPyvAgmAcl9qjEHvX0R438qJEfZflRvh718Sd9_Em-"
        "nlSCJw34JPJPGvBJxX2W92c94kVOXzXOq1z80LN-KMMfs53s9n_-_LOP7S-x9bFdze9__N9_3Mf2a2y3_98_3se2mUU_-Dp0_A_4316f59eX-"
        "fV1fv3BV5HfdMfX5_n1ZX59nV9_8OdIv-mOr8_z68v8-jq_ovs6677Ouq-z7uus-zrrlrNuOeuWs24565azLpl1yaxLZl0y65L5jwrS9_"
        "NfHByDZwUvCl4VHMm90Z8nvNGfKLzRnym80Z8qvJH4rcRvJX4r8VuJ30r8TuJ3Er-T-J3E7yT-KPFHiT9K_FHijxJ_kfiLxF8k_iLxF4m_"
        "SvxV4q8Sf5X4q8RLiZcSLyVeSrycxUbojNAZoTNCZ4TOCJ0ROiN0RuiM0JkPEn-Q-IPEHyT-ILHQGaEzQmeEzgid-STxJ4k_"
        "SfxJ4k8Sf5b4s8SfJf4s8WeJVRSjohgVxagoRkUxKopRUYyKYlQUo6IYFcWoKEZFMSqK-"
        "b0oicSJxInEicRaKSaVOJU4lTiVOJU4kziTOJM4kziTOJc4lziXOJc4l3iQeJB4kHiQeJDYS-"
        "wl9hJ7ib3EFxJfSHwh8YXEFxKvJV5LvJZ4LfFa4iBxkDhIHCQOEm8k3ki8kXgj8UbiS4kvJb6U-FLiS4mvJL6S-EriK4mvJL6W-"
        "Fria4mvJb6WeCvxVuKtxFuJtxLfSHwj8Y3ENxLfSLyTeCfxTuKdxDuJ9xLvJd5LvJd4L_GtxLcS30p8K_GtxHcS30l8J_"
        "GdxHcSf5f4u8TfJf4u8XeJ7yW-l_he4nuJ7yV-kPhB4geJHyR-kPhZ4meJnyV-lvh5FtufZ_"
        "ExeFbwouBVwVGszdxqM7fazK02c6vN3Gozt9rMrTZzq83cajO3Oo6tjmOr49jqOLY6jq12fqud32rnt9r5rXZ-q53faue32vmtdn6rnd9q-"
        "7Lavqy2L6vty2r7skZiI7GR2EhsJB4lHiUeJR4lHmdxpSlaaYpWmqKVpmilKVppilaaopWmaKUpWmmKVo8SP0r8KPGjxI-z-"
        "CSfxcfgWcGLglcFR3ElcSVxJXElcSXxicQnEp9IfCLxySxuNDcazY1Gc6PR3Gg0NxrNjUZzo9HcaDQ3Gs2NRnOj0dxoNDcazY1Gc6PRcdzoOG5"
        "0HDc6jhsdx42O40bHcaPjuNFx3Og4bnQcNzqOGx3HjY7jRsdxo-O40XHc6DhudBw3Oo4bzedG87nRfG40nxvN5_"
        "bXWXwMnhW8KHhVcBRriraaoq2maKsp2mqKtpqiraZoqynaaoq2mqKtdtFWu2irXbTVLtpqF-1-"
        "msXH4FnBi4JXBUexitKpKJ2K0qkonYriJHYSO4mdxO53sTg7cXbi7MTZibOrJa4lriWuJa4lbiRuJG4kbiRuJG4lbiVuJW4lbiXuJO4k7iTuJO"
        "4kPpX4VOJTiU8lPpX4TOIzic8kPpP4TOJvEn-T-JvE3yT-"
        "JvG5xOcSn0t8LvG5xLoxOt0YnW6MTjdGpxuj20q8lXgr8VbircS6yTjdZJxuMk43GaebjNNNxukm43STcbrJON1knG4yTjcZp5uM003G6SbjtA"
        "ad1qDTGnRag05r0GkNOq1BpzXotAad1qDTGnRag05r0GkNOq1Bp5uM003G6SbjdJNxusk43WScbjJONxmnm4zTTcYdJD5IfJD4IPFBYl17nK49"
        "Ttcep2uP07XHvUj8IvGLxC8Sv0j8KvGrxK8Sv0r8Oot70ehFoxeNXjR60RhUlEFFGVSUQUUZVJRBRRlUlEFFGVSUQUUZxHkQ50GcB3EexHkQ50"
        "GcB3EexHkQ5-FJ4ieJnyR-"
        "kvhpFnsdbV5Hm9fR5nW0eR1tXkeb19HmdbR5HW1eR5vXluu15XptuV5brteW63VV87qqeV3VvK5qXlc1byW2EluJrcRW4kLiQuJC4kLiQuJS4l"
        "LiUuJS4lJiXai8LlReFyqvC5XXhcrrQuV1ofK6UHldqLwuVH4l8UrilcQriVcS67TyOq28Tiuv08rrtPI6rbxOK6_"
        "Tyuu08jqtvE4rr9PK67TyOq28Tiuv08rrtPI6rbxOK6_Tyuu08jqtvE4rr9PK67TyOq28Tiuv08rrtPI6rYK-"
        "1AR9qQn6UhP0pSboS03Qj3tBP-4F_"
        "bgX9ONe0I97QbfcoFtu0C036JYbdMsNW4m3Em8l3kq8lVinVdBpFXRaBZ1WQadV0GkVdFoFnVZBp1XQaRV0WgWdVkGnVdBpFXRaBZ1WQadV0Gk"
        "VdFoFnVZBG2PQxhi0MQZtjEEbY9DGGLQxBm2MQRtj0MYYtD8H7c9B-3PQ_"
        "hy0PwftokG7aNAuGrSLBu2iQbto0C4atIsG7aJBu2jQF7GgL2JBX8SCvoiF376I_S_AtPTI");
    static string all_emojis_str = gzdecode(base64url_decode(packed_emojis).ok()).as_slice().str();
    constexpr size_t EMOJI_COUNT = 2370;
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

bool is_emoji(Slice str) {
  size_t i = str.substr(0, MAX_EMOJI_LENGTH + 4).find('\xE2');
  if (i == Slice::npos) {
    return is_emoji_element(str);
  }

  size_t start_pos = 0;
  for (; i + 3 < str.size(); i++) {
    if (str[i] == '\xE2' && str[i + 1] == '\x80' && str[i + 2] == '\x8D') {
      // zero-width joiner \u200D
      if (!is_emoji_element(str.substr(start_pos, i - start_pos))) {
        return false;
      }
      start_pos = i + 3;
      i += 2;
    }
  }
  return is_emoji_element(str.substr(start_pos));
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
