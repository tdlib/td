//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
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
        "eJxtmlly40iWRbdCs_rrr56H3WVmzBHE4A6AxCCFqiodcBFOKShSComawqyWglpAl1lvoP0wcdPK2vrDnY-mC8e75_"
        "kkRUzLdjEtu8WUbWIbYwuxbWPbxXZYTOuX2F4XU13GVi2m5mNsn2L7HNuX2JaxJbGlsWWx5bGZ2GxsRWw_FlMbP9s_xRbHb-"
        "P4bRyzjWN2b2N7F1t8tovjd_Hn3T62-N7uLrbH2KL2LGrO4vvO4vvOLhfTeXzneXzmPI57Hp87j3mdrxfT1_j5dRUbccz_"
        "a3z3Nnrbni2mq5jXVbX428VPb2P3Jovd-5TO0Fm6gq6kQ_d-Rbemq-kaujZ2H3-mO0Xf6PZ0B7pbuju673QPdEe6x9h9yul42_InOkZZ_"
        "kL3ho6slu_"
        "o3tN9oPtI94nuM90XuiVdQkf2S3wsGXl5GhkfS3ws8bHExxIfS3ws8bHEx5LsY9Vjd0Z3TveV7oLuj3RXdNd0uFzu6G7o8LvE7xK_S_wu7-"
        "nwu8TvEr_LJ7pnuhe6V7ofsUuwn2A_wX6C_QT7CfYT7CfYT7CfYD_BfoL9BPsJ9hPsJ9hPsJ9gP8F-gv0E-wn2E-wn2E-wn2A_wX6C_"
        "QT7CfYT7CfYT7Cf_Inuz3S_0jm6nm6g83SXdBu6kS7QbelAl4AuAV0CugR0CegS0CWgS0CXMFUS-CXwS-CXwC-BXwK_"
        "BHQJ6FLQpaBLQZeCLgVdCroUdCnoUtCloEtBl4IuBV0KuhR0KehS0KWgS0GXgi7Ffor9FPsp9lPsp9hPsZ9iP8V-iv0U-yn2U-yn2E-xn2I_"
        "xX6K_RT7KfZT7KfYT7GfYj_Ffor9FPsp9lPsp0yfFAYpDDIYZDDIYJDBIINBBoMMBhkMMhhkMMhgkOE3w2-G3wy_GX4z_"
        "Gb4zfCb4TfDb4bfDL8ZfjP8ZvjN8JvhN8Nvht8Mvxl-M_"
        "xmGMxwlOEow1GGowxHGY4yHGU4ynGU4yjHUY6jHEc5jnIc5TjKcZTjKMdRTlVzqppT1Zyq5rjMcZnjMsdljsucBZGzIHIWRM6CyFkQOQsiZ0Hk"
        "LIicBZGzIHIWRM6CyCGUQyiHUA6hHEI5hHII5RDKIZRDKIdQDqEcQjmEcgjlEMohlDMjcmZEzozIAZYzI3JmRM6MyOGXwy-HXw6_HH45_"
        "HL4GdAZqBmoGagZqBmoGagZqBmoGagZqBmoGagZqBmoGagZqBmoGagZqBmoGagZqBmoGagZqBmoGagZqBmoGagZqBmoGagZqBmoGagZqBmoGag"
        "ZqBmoGagZqBmoGagZqBmoGagZqBmoGagZqBmoGagZqBmoGagZqBmoGaiZEzVmnQWdZdZZ-Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-"
        "Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-Fn4WfhZ-"
        "Fn4WfhZ-Fn4WfhZ-Fn4FfAr4FfAr4BfAb8CfgX8CvgV8CvgV8CvgF8BvwJ-BfwK-BXwK-BXwK-AXwG_An4F_"
        "Ar4FfAr4FfAr4BfAb8CfgX8CvgV8CvgV8CvgF8BvwJ-BfwK-BXwK-BXwK-AXwG_An4F_"
        "Ar4FfAr4FfAr4BfAb8CfgX8CvgV8CvgV8CvgF8BvwJqJdRKqJVQK6FWQq2EWgm1Emol1EqolVAroVZCrYRaCbUSaiXUSqiVUCuhVkKthFoJtRJ"
        "qJdRKqJVQK6FWQq2EWgm1Emol1EqolVAroVZCrYRaCbUSaiXUSqiVUCuhVkKthFoJtRJqJdRKqJVQK6FWQq2EWgm1Emol1EqolVAroVZCrWTWV"
        "divsF9hv8J-hfMK5xXOK5xXOK9wXuG8wnmF8wrnFc4rnFc4r3Be4bzCeYXzCucVziucVzivcF7hvMJ5RX4r3rHiHSska9Jdk-"
        "6adNcskjXlril3Tblryl1T7ppy15S7ptw15a4pd025a8pd47fGb43fGr815a4xXWO6xnSN6RrTNQnVJFRjusZ0jeka0zWma0zXmK4xXWO6xnSN"
        "6RrTNaZrHNWYrjFdY7qm3DXlril3Tblryl1T7ppy15S7ptw15a4pd025a8pdU-"
        "6acteUu6bcNeWuwVnDr4ZfDb8afjX8Gvg18Gvg18CvgV8DvwZ-Dfwa-DXwa-DXwK-BXwO_Bn4N_"
        "FrGaxmvZbyW8VrGaxmvZbyW8VrGaxmvZbyW8VrGaxmvZbz2NB71aKlHSz1a6tFSj5Z6tNSjpR4t9WipR0s9WurRUo-"
        "WerTUo6UeLfVoqUdLPVrq0VKPlnq01KOlHi31aKlHSz1a6tFSj5Z6tNSjpR4t9WipR0s9WurRUo-"
        "WerTUo6UeLfVoqUdLPVrq0VKPlnq01KOlHh38Ovh18Ovg18Gvg18HoQ4kHUg6kHSA6ADRAaLDdIfpDtMdpjuy78i-"
        "I7WO1DpS60itI7WO1DpS60itI7ULWF3A6gJWF7C6gNUFrC5gdQGrC1hdwOoCVhe87QIujnQdBXUU1FFQR_"
        "aO7B3ZOwrqKKjDh8OHw4ejoI6COgrqKKjDm8Obw5vDmyNJR5KOJB1JOpJ0JOlI0pGkI0lHko4kHUk6kDgK6iioo6DulDgFdRTUUVAHNQc1BzUH"
        "NQc1BzUHNQcwRy0dtXTUsqeWPbXsqWVPLXtq2VPLnmXQswx6lkHPMuhZBj3Ueqj1UOuh1kOth1oPtR5qPdR6qPVQ66HWQ62HWg-"
        "1Hmo91Hqo9VDrodZDrYdaD7Ueaj3Ueqj1UOuh1kOth1oPtR5qPdR6qPVQ66HWQ62HWg-"
        "1Hmo91Hqo9VDrodZDrYdaD7WeudaDrgddD7oedAPoBtANoBtAN4BuAN3ANjLAb4DfAL8BfgP8BvgN8BvgN8BvgN8AvwF-A_wG-A3wG-A3wG-"
        "A3wC_AX4D_Ab4DfAb4DfAb4DfAL8BfgP8BvgN8BvgN8BvgN8AvwF-A_wG-A3wG-A3wG-A3wC_AX4D_"
        "Ab4DfAb4DfAb4DfAL8BfgP8BvgN8BvgN8DPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_"
        "Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw8_Dz8PPw2_"
        "DeBvG2zDehvE2jLdhvA3jbRhvw3gbxtsw3obxNow3UoCRAowUYKQAIwUYKcBIAUYKMFKAEbojdEfojtAdoTtCd4TuCN0RuiN0R-"
        "iO0B2hO0J3hO4I3RG6I3RH6I7QHaE7QneE7gjdEbojdEfojtAdoTtCd4TuCN0RGiM0RmiM0BihMUJjhMYIjREaIzRGaIzQGE80oDsCNsAlwCXA"
        "JcAlwCXAJTDhAhMugCSAJIAkgCSAJIAkgCSAJIAkgCSAJIAkYD9gP2A_YD9gP2A_YD9gP2A_4C3gLeAt4C3gLeAt4C3gLTws_rJZ_"
        "OV6Mf30vJh-_rSY3v45tuNiel_GVsW2im0dWx1bE9smtnExLS8XU5bFdhVbfD77FttNbPvYHmKLY2SPiyn-ljeton4VYvu-mNY_"
        "xfYUW3xfvOZO8ZY7xUvuFO-4U7ziTvHSOsWb6hSvolO8dU7xqjnFS-YU75hTvFhO8SI5xYvjFO-"
        "NU7wOTvE2OMXL4BSvfFO87E3NRWzxmSbqmz62-"
        "EwTc23iO5vXxRQvXFO8ak3xpjXFi9YU71lTvGFN8W41xavVFK9SU7wrTfEKNMUb0NR9jC36jFeTqStii_67-LOOn93Gdh9bzCNeNaaz-P6zqD-"
        "LHM-WsSWxxWfP4jvP4jvP4vvOoqezP8V2iC0-fx49n3-ILeZ77hbT1_izeDJP8WCetu9iex_bh8Vff9rF9rL46-d1bMyMd5T4HSV-xwJ_"
        "xzz8yOz7ePqrMhNlyfRYMj2WTI8l02PJ9FiyOpasjiWrY8nqWJ7-"
        "onr6oyHzMGUeprwoZQqmTMGUtZiyFjO21IwtNWNLzZjhGRM5YyJnDJAxQMZEzhglY5SMUTLWdsbazhgvO43HBM2Ym9npT1iYMac_Epx-"
        "j2Mrr9jKKxZuhfOKJyqmdMVjFdlXPFuxXCuW64qtaMVjK9Jdke6KdFcswxVcVtBYAXHFOlmdfhcD4polvGYJr1mua5brmuW6Jvs12a_"
        "Jfg3nNWtxzVBr8luT2vp06-S9Ha_sINSxB3Ss447HOtZxx7MdhepIqKMyHQa7w-IP05tfF3_474eMz384dfOXfzx185d_OnXzl38-"
        "dfOXfzl185d_PXXzl387dfOXfz9185f_OHXzl_88dfOX_zp185e_"
        "XXwYYrtUsFEwKggKtgquFdwo2Cu4VfBdwb2CBwVHBY8KnhW8KGDifPB8UeAVbBSMCoKCrYIrBdcKvim4UbBXcFBwq-"
        "C7gnsFDwqOCp4UPCt4VXBK_lLJXwrvpZK_"
        "VM6XyvlSOV8qZ4KdghsFewUHBbcK7hTcK3hU8KTgWcGLglcFp5w3orpRYhuh2yifjdLY6O0bPT7K8ijLowYcNeAop6NSHYV3FN5RyQfRCEojKI"
        "2gNILSCBpwqzS2mjZbkd8qn61KsFViWyW21Uu3Ir_Vu7YivxXwrWbL9u_f_qDgqOBRwbOCE_"
        "kr2bnSK670iisNeKVxrjTOtfBey9e1fF0r52sNeK0BrwXqWjlf6xXXyvla7_qmAb9pnG96_Ju876TZieFODHdiuNPjO6Wx-_"
        "vH7xU8K3hVcJpRNyrljUp5I-83esWNGN5owBvZuZGdG6EjeFJwetder9hr5L2o7mVwr9myl9O9nO719r3I72V5L8t7odvL-14l2CvnvXLeK-"
        "e9ct4r571AEbwo-N3FidhBdg6yc5CLg1wc5OIghgclf1CqB6V6UIYH5XPQu27l9E4vvdO77vSuO73rTsTuROxOL737-3EOCu4VPCg4KnhWcPL-"
        "XW-_19vv5eJej98r-Xs9_qCnCLyCSwUbBaOCrYIrBdcKvinYKbhRsFdwUHCr4F7Bg4KjgicFLwpeFZzIH5X8UTkflfNR5I_K-"
        "aicj0r1qFSPSvWoVI9K9ahUj0r1qAyPyvAomEcl9qjEHvX2R438qJEfZflRvh71-JMef5KvJ5XgSQM-ifyTBnxScZ_l_VmveJHTV43zKhc_"
        "9K4fyvDHbCe7_Z8__uxj-3NsfWzX8_cf__eHh9h-je32__vhfWzbWfSD34JO_wj-2-fz_Pkyf77Onz_4DeQ33enzef58mT9f588f_Jeg33Snz-"
        "f582X-fJ0_0X2ZdV9m3ZdZ92XWfZl1y1m3nHXLWbecdctZl8y6ZNYlsy6Zdcn8D_vpu_lf_U_Bs4IXBa8KTuR-0X8R-EX_TeAX_VeBX_"
        "TfBX6R-I3EbyR-I_Ebid9I_FbitxK_lfitxG8l_iDxB4k_SPxB4g8Sf5b4s8SfJf4s8WeJv0j8ReIvEn-R-"
        "IvES4mXEi8lXkq8nMVG6IzQGaEzQmeEzgidETojdEbojNCZ9xK_l_i9xO8lfi-x0BmhM0JnhM4Infko8UeJP0r8UeKPEn-S-JPEnyT-"
        "JPEniVUUo6IYFcWoKEZFMSqKUVGMimJUFKOiGBXFqChGRTEqivm9KInEicSJxInEWikmlTiVOJU4lTiVOJM4kziTOJM4kziXOJc4lziXOJd4kH"
        "iQeJB4kHiQ2EvsJfYSe4m9xJcSX0p8KfGlxJcSbyTeSLyReCPxRuIgcZA4SBwkDhJvJd5KvJV4K_FW4iuJryS-kvhK4iuJryW-"
        "lvha4muJryXeSbyTeCfxTuKdxDcS30h8I_GNxDcS7yXeS7yXeC_"
        "xXuKDxAeJDxIfJD5IfCvxrcS3Et9KfCvxncR3Et9JfCfxncTfJf4u8XeJv0v8XeJ7ie8lvpf4XuJ7iR8kfpD4QeIHiR8kfpb4WeJniZ8lfp7F9"
        "udZfAqeFbwoeFVwEmt_ttqfrfZnq_3Zan-22p-t9mer_"
        "dlqf7ban61OWKsT1uqEtTphrU5Yq83cajO32sytNnOrzdxqM7fazK02c6vN3Gozt9qRrHYkqx3Jakey2pGskdhIbCQ2EhuJR4lHiUeJR4nHWVx"
        "pilaaopWmaKUpWmmKVpqilaZopSlaaYpWmqLVo8SPEj9K_"
        "Cjx4yxe5bP4FDwreFHwquAkriSuJK4kriSuJF5JvJJ4JfFK4tUsbjQ3Gs2NRnOj0dxoNDcazY1Gc6PR3Gg0NxrNjUZzo9HcaDQ3Gs2NRnOj0Qn"
        "b6IRtdMI2OmEbnbCNTthGJ2yjE7bRCdvohG10wjY6YRudsI1O2EYnbKMTttEJ2-"
        "iEbXTCNjphG83nRvO50XxuNJ8bzef211l8Cp4VvCh4VXASa4q2mqKtpmirKdpqiraaoq2maKsp2mqKtpqirXbRVrtoq1201S7aahftfprFp-"
        "BZwYuCVwUnsYrSqSiditKpKJ2K4iR2EjuJncTud7E4O3F24uzE2YmzqyWuJa4lriWuJW4kbiRuJG4kbiRuJW4lbiVuJW4l7iTuJO4k7iTuJD6T"
        "-EziM4nPJD6T-Fzic4nPJT6X-FzirxJ_lfirxF8l_irxhcQXEl9IfCHxhcS6BDpdAp0ugU6XQKdLoNtJvJN4J_"
        "FO4p3Eusk43WScbjJONxmnm4zTTcbpJuN0k3G6yTjdZJxuMk43GaebjNNNxukm47QGndag0xp0WoNOa9BpDTqtQac16LQGndag0xp0WoNOa9Bp"
        "DTqtQaebjNNNxukm43STcbrJON1knG4yTjcZp5uM003GHSU-SnyU-CjxUeIXiV8kfpH4ReIXiV8lfpX4VeJXiV9ncS-DvQz2MtjLYC-"
        "DgzgP4jyI8yDOgzgP4jyI8yDOgzgP4jwI3SB0g9ANQjcI3SB0g9ANQjcI3SB0w5PETxI_"
        "Sfwk8dMs9jqtvE4rr9PK67TyOq28Tiuv08rrtPI6rbxOK69d1GsX9dpFvXZRr13U6_bldfvyun153b68bl_"
        "eSmwlthJbia3EhcSFxIXEhcSFxKXEpcSlxKXEpcS6I3ndkbzuSF53JK87ktcdyeuO5HVH8rojed2R_"
        "FritcRridcSryXWAeR1AHkdQF4HkNcB5HUAeR1AXgeQ1wHkdQB5HUBeB5DXAeR1AHkdQF4HkNcB5HUAeR1AXgeQ1wHkdQB5HUBeB5DXAeR1AHk"
        "dQF4HkNcB5HUABf2eEvR7StDvKUG_pwT9nhL0J7igP8EF_Qku6E9wQX-"
        "CC7q4Bl1cgy6uQRfXoItr2Em8k3gn8U7incQ6gIIOoKADKOgACjqAgg6goAMo6AAKOoCCDqCgAyjoAAo6gIIOoKADKOgACjqAgg6goAMo6AAK2"
        "hiDNsagjTFoYwzaGIM2xqCNMWhjDNoYgzbGoP05aH8O2p-D9ueg_TloFw3aRYN20aBdNPy2i_4vsAI1eQ");
    static string all_emojis_str = gzdecode(base64url_decode(packed_emojis).ok()).as_slice().str();
    constexpr size_t EMOJI_COUNT = 2326;
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
