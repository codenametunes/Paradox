#pragma once
#ifndef STRUCTURE_H
#define STRUCTURE_H

#include "main.h"

typedef struct Structure {
    int id;
    const char* name;

	// 3d array of blocks that make up the structure
    // specify which blocks will be used
	// specify the size of the structure in x, y, z dimensions
    // customizable settings for height manipulation for every structure
    // 
} Structure;

static const Structure STRUCTURES[] = {
    {
        // this will be the basic minecraft tree
		// BLOCK_LEAVES and BLOCK_LOG will be used
    }
};