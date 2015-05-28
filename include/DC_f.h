/*  Copyright 2014 - UVSQ
    Authors list: Loïc Thébault, Eric Petit

    This file is part of the DC-lib.

    DC-lib is free software: you can redistribute it and/or modify it under the
    terms of the GNU Lesser General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later version.

    DC-lib is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
    PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along with
    the DC-lib. If not, see <http://www.gnu.org/licenses/>. */

#ifndef DC_F_H
#define DC_F_H

#include "DC.h"

extern "C" {
    // Get time of day
    double dc_get_time_ ();

    // RDTSC
    uint64_t dc_get_cycles_ ();

    // Wrapper used to get the root of the D&C tree before calling the real tree
    // traversal
    void dc_tree_traversal_ (void (*userSeqFct) (void *, DCargs_t *),
                             void (*userVecFct) (void *, DCargs_t *), void *userArgs);

    // Permute "tab" 2D array of double using node permutation
    void dc_permute_double_2d_array_ (double *tab, int *nbItem, int *dimItem);

    // Permute "tab" 2D array of int using "perm"
    void dc_permute_int_2d_array_ (int *tab, int *perm, int *nbItem, int *dimItem,
                                   int *offset);

    // Permute "tab" 1D array of int using node permutation
    void dc_permute_int_1d_array_ (int *tab, int *size);

    // Renumber "tab" array of int using node permutation
    void dc_renumber_int_array_ (int *tab, int *size);

    // Create permutation array from partition array
    void dc_create_permutation_ (int *perm, int *part, int *size, int *nbPart);

    // Read the D&C tree and the permutation functions
    void dc_read_tree_ (char *treePath, int *nbElem, int *nbNodes);

    // Store the D&C tree and the permutation functions to a binary file
    void dc_store_tree_ (char *treePath, int *nbElem, int *nbNodes);

    // Wrapper used to get the root of the D&C tree before computing the edge intervals
    // for CSR reset
    void dc_finalize_tree_ (int *nodeToNodeRow, int *elemToNode);

    // Create the D&C tree and the permutations
    void dc_create_tree_ (int *elemToNode, int *nbElem, int *dimElem, int *nbNodes);
}

#endif
