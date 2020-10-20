/*
 *                This source code is part of
 *
 *                          HelFEM
 *                             -
 * Finite element methods for electronic structure calculations on small systems
 *
 * Written by Susi Lehtola, 2018-
 * Copyright (c) 2018- Susi Lehtola
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */
#include "helfem.h"
#include <iostream>

using namespace std;

bool helfem::verbose = false;

void helfem::set_verbosity(bool new_verbosity) {
  if (helfem::verbose && new_verbosity)
    printf("HelFEM library already in verbose mode.\n");
  else if (!helfem::verbose && new_verbosity)
    printf("HelFEM library set to verbose mode.\n");
  helfem::verbose = new_verbosity;
}

std::string helfem::version() { return __HELFEM_VERSION__; }
