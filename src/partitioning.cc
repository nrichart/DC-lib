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

#ifdef TREE_CREATION

#include <cstring>
#include <cmath>
#ifdef CILK
    #include <cilk/cilk.h>
#endif
#include <pthread.h>
#include <metis.h>

#include "tools.h"
#include "permutations.h"
#include "tree_creation.h"
#include "partitioning.h"

extern tree_t *treeHead;
extern int *elemPerm, *nodePerm, *nodeOwner;
#ifdef MULTITHREADED_COMM
    extern int commLevel;
#endif

pthread_mutex_t metisMutex = PTHREAD_MUTEX_INITIALIZER;

// Create a nodal graph from a tetrahedron mesh (created from METIS)
void mesh_to_nodal (int *graphIndex, int *graphValue, int *elemToNode, int nbElem,
                    int dimElem, int nbNodes)
{
    int nEdges, *nPtr, *nInd, *marker;

    nPtr = new int [nbNodes + 1] ();
    for (int i = 0; i < dimElem * nbElem; i++) {
        nPtr[elemToNode[i]]++;
    }
    for (int i = 1; i < nbNodes; i++) {
        nPtr[i] += nPtr[i-1];
    }
    for (int i = nbNodes; i > 0; i--) {
        nPtr[i] = nPtr[i-1];
    }
    nPtr[0] = 0;

    nInd = new int [nPtr[nbNodes]];
    for (int i = 0; i < nbElem; i++) {
        for (int j = 0; j < dimElem; j++) {
            nInd[nPtr[elemToNode[i*dimElem+j]]++] = i;
        }
    }
    for (int i = nbNodes; i > 0; i--) {
        nPtr[i] = nPtr[i-1];
    }
    nPtr[0] = 0;

    marker = new int [nbNodes];
    memset (marker, -1, nbNodes * sizeof (int));
    nEdges = graphIndex[0] = 0;
    for (int i = 0; i < nbNodes; i++) {
        marker[i] = i;
        for (int j = nPtr[i]; j < nPtr[i+1]; j++) {
            int jj = dimElem * nInd[j];
            for (int k = 0; k < dimElem; k++) {
                int kk = elemToNode[jj];
                if (marker[kk] != i) {
                    marker[kk] = i;
                    graphValue[nEdges++] = kk;
                }
                jj++;
            }
        }
        graphIndex[i+1] = nEdges;
    }
    delete[] marker, delete[] nInd, delete[] nPtr;
}

// Create local elemToNode array containing elements indexed contiguously from 0 to
// nbElem and return the number of nodes accessed
int create_sepToNode (int *sepToNode, int *elemToNode, int firstSepElem,
                      int lastSepElem, int dimElem)
{
    int nbNodes = 0;
    int *tmp = new int [(lastSepElem - firstSepElem + 1) * dimElem];

    for (int i = firstSepElem*dimElem, j = 0; i < (lastSepElem+1)*dimElem; i++, j++) {
        int newNode = 0, oldNode = elemToNode[i];
        bool isNew = true;
        for (newNode = 0; newNode < nbNodes; newNode++) {
            if (oldNode == tmp[newNode]) {
                isNew = false;
                break;
            }
        }
        if (isNew) {
            tmp[nbNodes] = oldNode;
            nbNodes++;
        }
        sepToNode[j] = newNode;
    }
    delete[] tmp;
    return nbNodes;
}

// D&C partitioning of separators with more than MAX_ELEM_PER_PART elements
void sep_partitioning (tree_t &tree, int *elemToNode, int globalNbElem, int dimElem,
                       int firstSepElem, int lastSepElem, int firstNode, int lastNode,
                       int curNode)
{
    // If there is not enough element in the separator
    int nbSepElem = lastSepElem - firstSepElem + 1;
    int nbSepPart = ceil (nbSepElem / (double)MAX_ELEM_PER_PART);
    if (nbSepPart < 2 || nbSepElem <= MAX_ELEM_PER_PART) {

        #ifdef MULTITHREADED_COMM
            // Set the node accessed by current D&C leaf
            fill_node_owner (elemToNode, firstSepElem, lastSepElem, dimElem, firstNode,
                             lastNode, curNode, true);
        #endif

        // Initialize the leaf
        init_dc_tree (tree, firstSepElem, lastSepElem, 0, firstNode, lastNode, true,
                      true);

        // End of recursion
        return;
    }

    // Create temporal elemToNode containing the separator elements
    int *sepToNode = new int [nbSepElem * dimElem];
    int nbSepNodes = create_sepToNode (sepToNode, elemToNode, firstSepElem,
                                       lastSepElem, dimElem);

    // Configure METIS & compute the node partitioning of the separators
    int constraint = 1, objVal;
    int *graphIndex = new int [nbSepNodes + 1];
    int *graphValue = new int [nbSepNodes * 15];
    int *nodePart   = new int [nbSepNodes];
    mesh_to_nodal (graphIndex, graphValue, sepToNode, nbSepElem, dimElem, nbSepNodes);

    // Execution is correct without mutex although cilkscreen detects many race
    // conditions. Check if the problem is solved with future version of METIS (5.0)
    pthread_mutex_lock (&metisMutex);
    METIS_PartGraphRecursive (&nbSepNodes, &constraint, graphIndex, graphValue,
                              nullptr, nullptr, nullptr, &nbSepPart, nullptr, nullptr,
                              nullptr, &objVal, nodePart);
    pthread_mutex_unlock (&metisMutex);
    delete[] graphValue, delete[] graphIndex;

    // Create the separator D&C tree
    tree_creation (tree, elemToNode, sepToNode, nodePart, nullptr, globalNbElem,
                   dimElem, 0, nbSepPart-1, firstSepElem, lastSepElem, firstNode,
                   lastNode, 0, curNode, true);
    delete[] nodePart, delete[] sepToNode;
}

// Divide & Conquer partitioning
void partitioning (int *elemToNode, int nbElem, int dimElem, int nbNodes)
{
    // Fortran to C elemToNode conversion
    #ifdef OMP
        #pragma omp parallel for
        for (int i = 0; i < nbElem * dimElem; i++) {
    #elif CILK
        cilk_for (int i = 0; i < nbElem * dimElem; i++) {
    #endif
        elemToNode[i]--;
    }

    // Configure METIS & compute the node partitioning of the mesh
    int nbPart = ceil (nbElem / (double)MAX_ELEM_PER_PART);
    int constraint = 1, objVal;
    int *graphIndex = new int [nbNodes + 1];
    int *graphValue = new int [nbNodes * 15];
    int *nodePart   = new int [nbNodes];
    #ifdef MULTITHREADED_COMM
        commLevel = ceil ((double)log2 (nbPart) / 4.);
    #endif
    mesh_to_nodal (graphIndex, graphValue, elemToNode, nbElem, dimElem, nbNodes);
    METIS_PartGraphRecursive (&nbNodes, &constraint, graphIndex, graphValue, nullptr,
                              nullptr, nullptr, &nbPart, nullptr, nullptr, nullptr,
                              &objVal, nodePart);
    delete[] graphValue, delete[] graphIndex;

    // Create node permutation from node partition
    DC_create_permutation (nodePerm, nodePart, nbNodes, nbPart);

    // Compute the number of nodes per partition
    int *nodePartSize = new int [nbPart] ();
    for (int i = 0; i < nbNodes; i++) {
        nodePartSize[nodePart[i]]++;
    }

    // Initialize the global element permutation
    #ifdef OMP
        #pragma omp parallel for
        for (int i = 0; i < nbElem; i++) {
    #elif CILK
        cilk_for (int i = 0; i < nbElem; i++) {
    #endif
        elemPerm[i] = i;
    }

    // Create D&C tree
    #ifdef OMP
        #pragma omp parallel
        #pragma omp single nowait
    #endif
    tree_creation (*treeHead, elemToNode, nullptr, nodePart, nodePartSize, nbElem,
                   dimElem, 0, nbPart-1, 0, nbElem-1, 0, nbNodes-1, 0, 0, false);
    delete[] nodePartSize, delete[] nodePart;

    // C to Fortran elemToNode conversion
    #ifdef OMP
        #pragma omp parallel for
        for (int i = 0; i < nbElem * dimElem; i++) {
    #elif CILK
    	cilk_for (int i = 0; i < nbElem * dimElem; i++) {
    #endif	
        elemToNode[i]++;
    }
}

#endif
