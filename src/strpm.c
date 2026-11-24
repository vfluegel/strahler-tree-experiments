/*
 * Copyright 2017-2018 Tom van Dijk, Johannes Kepler University Linz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 * Derived from Oink (https://github.com/trolando/oink) and Vanessa Flügel's
 * fork (https://github.com/vfluegel/oink). This file is licensed under
 * Apache-2.0 rather than the repository's MIT license. Apache-2.0 → MIT
 * relicensing requires the copyright holders' consent; until that consent
 * is obtained this file remains Apache-2.0.
 * Changes: ported to C23 and modified for tree output.
 */

#include <assert.h>

#include "pm_internal.h"

static int minimum(int const left, int const right) {
  return left < right ? left : right;
}

static int distinct(PMMeasure const *measure, int const begin, int const end) {
  int count = begin < end ? 1 : 0;
  for (int i = begin + 1; i < end; i++) {
    count += measure->levels[i] != measure->levels[i - 1];
  }
  return count;
}

void strpm_truncate(PMMeasure *measure, int const pindex, int const k,
                    int const t) {
  if (measure->top || k == 1) {
    return;
  }
  int retained = 0;
  while (retained < measure->length && measure->levels[retained] <= pindex) {
    retained++;
  }
  int const strings = distinct(measure, 0, retained);
  int const remaining = k - 1 - strings;
  measure->length = retained;
  if (remaining == 0) {
    return;
  }
  /* Choose the first canonical leaf with this prefix. Erasing the suffix
   * alone would leave the fixed-k leaf domain. Spend the remaining bit budget
   * on its first zero string, then add the other required leading zeros. */
  int const first_length = t - (retained - strings) + 1;
  for (int i = 0; i < first_length; i++) {
    measure->bits[measure->length] = 0;
    measure->levels[measure->length++] = pindex + 1;
  }
  for (int i = 1; i < remaining; i++) {
    measure->bits[measure->length] = 0;
    measure->levels[measure->length++] = pindex + 1 + i;
  }
}

void strpm_successor(PMMeasure *measure, int const pindex, int const depth,
                     int const k, int const t) {
  if (measure->top) {
    return;
  }
  if (measure->length == 0 || pindex < 0) {
    /* No strictly greater prefix exists at the empty cutoff. */
    measure->top = true;
    return;
  }
  bool skip_level = false;
  int i = measure->length - 1;
  while (i >= 0 && measure->levels[i] > pindex) {
    measure->bits[i--] = 0;
  }
  int nes = distinct(measure, 0, i + 1);
  if (nes == k) {
    while (i == measure->length - 1 ||
           (i >= 0 && measure->levels[i] == measure->levels[i + 1])) {
      measure->bits[i--] = 0;
    }
  }
  while (i >= 0) {
    if (i == measure->length - 1 ||
        measure->levels[i] != measure->levels[i + 1]) {
      int nlb = i + 1 - nes;
      nes--;
      if (nlb < t) {
        int const level =
            minimum(i + 1 == measure->length ? measure->levels[i]
                                             : measure->levels[i + 1] - 1,
                    pindex);
        i++;
        if (i != 0 &&
            (i == measure->length || measure->levels[i - 1] == level)) {
          nlb++;
        } else {
          nes++;
        }
        int const total_nes = minimum(nes + depth - level, k - 1);
        if (total_nes + t > measure->length) {
          int const old_length = measure->length;
          measure->length = total_nes + t;
          for (int j = old_length; j < measure->length; j++) {
            measure->bits[j] = 0;
          }
          measure->bits[i] = 1;
          for (int j = i; j < measure->length; j++) {
            measure->levels[j] = level;
          }
          i += t - nlb + 1;
        } else {
          measure->bits[i] = 1;
          measure->levels[i] = level;
          int j = 1;
          while (nlb + j <= t) {
            measure->bits[i + j] = 0;
            measure->levels[i + j] = level;
            j++;
          }
          i += j;
        }
        break;
      }
      if (i == 0 || measure->levels[i - 1] != measure->levels[i]) {
        measure->bits[i--] = 0;
        continue;
      }
    }
    if (measure->bits[i] == 0) {
      if (i == 0 || measure->levels[i - 1] != measure->levels[i]) {
        if (distinct(measure, i, measure->length) ==
            depth - measure->levels[i]) {
          i--;
          continue;
        }
        nes--;
        skip_level = true;
      }
      break;
    }
    measure->bits[i--] = 0;
  }
  if (i == -1) {
    if (measure->levels[0] == 0) {
      measure->top = true;
      return;
    }
    i = 0;
    measure->bits[0] = 1;
    measure->levels[0] = minimum(measure->levels[0], pindex + 1) - 2;
    if (t + k - 1 > measure->length) {
      int const old_length = measure->length;
      measure->length = t + k - 1;
      for (int j = old_length; j < measure->length; j++) {
        measure->bits[j] = 0;
      }
      for (int j = 0; j < measure->length; j++) {
        measure->levels[j] = measure->levels[0];
      }
    }
    skip_level = true;
    nes--;
  }
  int const level = measure->levels[skip_level ? i : i - 1] + 1;
  int const needed = minimum(k - 2 - nes, depth - level);
  if (needed == 0) {
    measure->length = i;
  } else {
    int next_level = level;
    while (measure->length >= i + needed) {
      assert(i >= 0 && i < measure->length && next_level < depth);
      measure->levels[i++] = next_level;
    }
    while (i < measure->length) {
      next_level++;
      assert(next_level < depth);
      measure->levels[i++] = next_level;
    }
  }
  assert(measure->length - distinct(measure, 0, measure->length) <= t);
}
