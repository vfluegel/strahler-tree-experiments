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

#ifndef PM_INTERNAL_H
#define PM_INTERNAL_H 1

#include "pm_lift.h"

/* Working arrays have capacity k-1+t (Strahler) or bits (succinct). */
void strpm_successor(PMMeasure *measure, int pindex, int depth, int k, int t);
/* First canonical leaf with the same prefix through pindex; preserve TOP. */
void strpm_truncate(PMMeasure *measure, int pindex, int k, int t);
void sspm_successor(PMMeasure *measure, int pindex, int depth);
void sspm_truncate(PMMeasure *measure, int pindex);
void pm_error(PMError *error, char const *message);

#endif
