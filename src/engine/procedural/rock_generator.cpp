#include "rock_generator.hpp"
#include "engine/model_loader.hpp"
#include "engine/gpu/gpu_types.hpp"
#include <glm/glm.hpp>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

namespace mmo::engine::procedural {

// ============================================================================
// Marching cubes edge and triangle tables
// ============================================================================

// Edge table: for each of 256 cube configurations, which edges are intersected
static const int edge_table[256] = {
    0x0,0x109,0x203,0x30a,0x406,0x50f,0x605,0x70c,0x80c,0x905,0xa0f,0xb06,0xc0a,0xd03,0xe09,0xf00,
    0x190,0x99,0x393,0x29a,0x596,0x49f,0x795,0x69c,0x99c,0x895,0xb9f,0xa96,0xd9a,0xc93,0xf99,0xe90,
    0x230,0x339,0x33,0x13a,0x636,0x73f,0x435,0x53c,0xa3c,0xb35,0x83f,0x936,0xe3a,0xf33,0xc39,0xd30,
    0x3a0,0x2a9,0x1a3,0xaa,0x7a6,0x6af,0x5a5,0x4ac,0xbac,0xaa5,0x9af,0x8a6,0xfaa,0xea3,0xda9,0xca0,
    0x460,0x569,0x663,0x76a,0x66,0x16f,0x265,0x36c,0xc6c,0xd65,0xe6f,0xf66,0x86a,0x963,0xa69,0xb60,
    0x5f0,0x4f9,0x7f3,0x6fa,0x1f6,0xff,0x3f5,0x2fc,0xdfc,0xcf5,0xfff,0xef6,0x9fa,0x8f3,0xbf9,0xaf0,
    0x650,0x759,0x453,0x55a,0x256,0x35f,0x55,0x15c,0xe5c,0xf55,0xc5f,0xd56,0xa5a,0xb53,0x859,0x950,
    0x7c0,0x6c9,0x5c3,0x4ca,0x3c6,0x2cf,0x1c5,0xcc,0xfcc,0xec5,0xdcf,0xcc6,0xbca,0xac3,0x9c9,0x8c0,
    0x8c0,0x9c9,0xac3,0xbca,0xcc6,0xdcf,0xec5,0xfcc,0xcc,0x1c5,0x2cf,0x3c6,0x4ca,0x5c3,0x6c9,0x7c0,
    0x950,0x859,0xb53,0xa5a,0xd56,0xc5f,0xf55,0xe5c,0x15c,0x55,0x35f,0x256,0x55a,0x453,0x759,0x650,
    0xaf0,0xbf9,0x8f3,0x9fa,0xef6,0xfff,0xcf5,0xdfc,0x2fc,0x3f5,0xff,0x1f6,0x6fa,0x7f3,0x4f9,0x5f0,
    0xb60,0xa69,0x963,0x86a,0xf66,0xe6f,0xd65,0xc6c,0x36c,0x265,0x16f,0x66,0x76a,0x663,0x569,0x460,
    0xca0,0xda9,0xea3,0xfaa,0x8a6,0x9af,0xaa5,0xbac,0x4ac,0x5a5,0x6af,0x7a6,0xaa,0x1a3,0x2a9,0x3a0,
    0xd30,0xc39,0xf33,0xe3a,0x936,0x83f,0xb35,0xa3c,0x53c,0x435,0x73f,0x636,0x13a,0x33,0x339,0x230,
    0xe90,0xf99,0xc93,0xd9a,0xa96,0xb9f,0x895,0x99c,0x69c,0x795,0x49f,0x596,0x29a,0x393,0x99,0x190,
    0xf00,0xe09,0xd03,0xc0a,0xb06,0xa0f,0x905,0x80c,0x70c,0x605,0x50f,0x406,0x30a,0x203,0x109,0x0
};

// Triangle table: for each of 256 cube configs, up to 5 triangles (15 edge indices, -1 terminated)
static const int tri_table[256][16] = {
    {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,8,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,1,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,8,3,9,8,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,2,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,8,3,1,2,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {9,2,10,0,2,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {2,8,3,2,10,8,10,9,8,-1,-1,-1,-1,-1,-1,-1},
    {3,11,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,11,2,8,11,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,9,0,2,3,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,11,2,1,9,11,9,8,11,-1,-1,-1,-1,-1,-1,-1},
    {3,10,1,11,10,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,10,1,0,8,10,8,11,10,-1,-1,-1,-1,-1,-1,-1},
    {3,9,0,3,11,9,11,10,9,-1,-1,-1,-1,-1,-1,-1},
    {9,8,10,10,8,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,7,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,3,0,7,3,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,1,9,8,4,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,1,9,4,7,1,7,3,1,-1,-1,-1,-1,-1,-1,-1},
    {1,2,10,8,4,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {3,4,7,3,0,4,1,2,10,-1,-1,-1,-1,-1,-1,-1},
    {9,2,10,9,0,2,8,4,7,-1,-1,-1,-1,-1,-1,-1},
    {2,10,9,2,9,7,2,7,3,7,9,4,-1,-1,-1,-1},
    {8,4,7,3,11,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {11,4,7,11,2,4,2,0,4,-1,-1,-1,-1,-1,-1,-1},
    {9,0,1,8,4,7,2,3,11,-1,-1,-1,-1,-1,-1,-1},
    {4,7,11,9,4,11,9,11,2,9,2,1,-1,-1,-1,-1},
    {3,10,1,3,11,10,7,8,4,-1,-1,-1,-1,-1,-1,-1},
    {1,11,10,1,4,11,1,0,4,7,11,4,-1,-1,-1,-1},
    {4,7,8,9,0,11,9,11,10,11,0,3,-1,-1,-1,-1},
    {4,7,11,4,11,9,9,11,10,-1,-1,-1,-1,-1,-1,-1},
    {9,5,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {9,5,4,0,8,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,5,4,1,5,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {8,5,4,8,3,5,3,1,5,-1,-1,-1,-1,-1,-1,-1},
    {1,2,10,9,5,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {3,0,8,1,2,10,4,9,5,-1,-1,-1,-1,-1,-1,-1},
    {5,2,10,5,4,2,4,0,2,-1,-1,-1,-1,-1,-1,-1},
    {2,10,5,3,2,5,3,5,4,3,4,8,-1,-1,-1,-1},
    {9,5,4,2,3,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,11,2,0,8,11,4,9,5,-1,-1,-1,-1,-1,-1,-1},
    {0,5,4,0,1,5,2,3,11,-1,-1,-1,-1,-1,-1,-1},
    {2,1,5,2,5,8,2,8,11,4,8,5,-1,-1,-1,-1},
    {10,3,11,10,1,3,9,5,4,-1,-1,-1,-1,-1,-1,-1},
    {4,9,5,0,8,1,8,10,1,8,11,10,-1,-1,-1,-1},
    {5,4,0,5,0,11,5,11,10,11,0,3,-1,-1,-1,-1},
    {5,4,8,5,8,10,10,8,11,-1,-1,-1,-1,-1,-1,-1},
    {9,7,8,5,7,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {9,3,0,9,5,3,5,7,3,-1,-1,-1,-1,-1,-1,-1},
    {0,7,8,0,1,7,1,5,7,-1,-1,-1,-1,-1,-1,-1},
    {1,5,3,3,5,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {9,7,8,9,5,7,10,1,2,-1,-1,-1,-1,-1,-1,-1},
    {10,1,2,9,5,0,5,3,0,5,7,3,-1,-1,-1,-1},
    {8,0,2,8,2,5,8,5,7,10,5,2,-1,-1,-1,-1},
    {2,10,5,2,5,3,3,5,7,-1,-1,-1,-1,-1,-1,-1},
    {7,9,5,7,8,9,3,11,2,-1,-1,-1,-1,-1,-1,-1},
    {9,5,7,9,7,2,9,2,0,2,7,11,-1,-1,-1,-1},
    {2,3,11,0,1,8,1,7,8,1,5,7,-1,-1,-1,-1},
    {11,2,1,11,1,7,7,1,5,-1,-1,-1,-1,-1,-1,-1},
    {9,5,8,8,5,7,10,1,3,10,3,11,-1,-1,-1,-1},
    {5,7,0,5,0,9,7,11,0,1,0,10,11,10,0,-1},
    {11,10,0,11,0,3,10,5,0,8,0,7,5,7,0,-1},
    {11,10,5,7,11,5,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {10,6,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,8,3,5,10,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {9,0,1,5,10,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,8,3,1,9,8,5,10,6,-1,-1,-1,-1,-1,-1,-1},
    {1,6,5,2,6,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,6,5,1,2,6,3,0,8,-1,-1,-1,-1,-1,-1,-1},
    {9,6,5,9,0,6,0,2,6,-1,-1,-1,-1,-1,-1,-1},
    {5,9,8,5,8,2,5,2,6,3,2,8,-1,-1,-1,-1},
    {2,3,11,10,6,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {11,0,8,11,2,0,10,6,5,-1,-1,-1,-1,-1,-1,-1},
    {0,1,9,2,3,11,5,10,6,-1,-1,-1,-1,-1,-1,-1},
    {5,10,6,1,9,2,9,11,2,9,8,11,-1,-1,-1,-1},
    {6,3,11,6,5,3,5,1,3,-1,-1,-1,-1,-1,-1,-1},
    {0,8,11,0,11,5,0,5,1,5,11,6,-1,-1,-1,-1},
    {3,11,6,0,3,6,0,6,5,0,5,9,-1,-1,-1,-1},
    {6,5,9,6,9,11,11,9,8,-1,-1,-1,-1,-1,-1,-1},
    {5,10,6,4,7,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,3,0,4,7,3,6,5,10,-1,-1,-1,-1,-1,-1,-1},
    {1,9,0,5,10,6,8,4,7,-1,-1,-1,-1,-1,-1,-1},
    {10,6,5,1,9,7,1,7,3,7,9,4,-1,-1,-1,-1},
    {6,1,2,6,5,1,4,7,8,-1,-1,-1,-1,-1,-1,-1},
    {1,2,5,5,2,6,3,0,4,3,4,7,-1,-1,-1,-1},
    {8,4,7,9,0,5,0,6,5,0,2,6,-1,-1,-1,-1},
    {7,3,9,7,9,4,3,2,9,5,9,6,2,6,9,-1},
    {3,11,2,7,8,4,10,6,5,-1,-1,-1,-1,-1,-1,-1},
    {5,10,6,4,7,2,4,2,0,2,7,11,-1,-1,-1,-1},
    {0,1,9,4,7,8,2,3,11,5,10,6,-1,-1,-1,-1},
    {9,2,1,9,11,2,9,4,11,7,11,4,5,10,6,-1},
    {8,4,7,3,11,5,3,5,1,5,11,6,-1,-1,-1,-1},
    {5,1,11,5,11,6,1,0,11,7,11,4,0,4,11,-1},
    {0,5,9,0,6,5,0,3,6,11,6,3,8,4,7,-1},
    {6,5,9,6,9,11,4,7,9,7,11,9,-1,-1,-1,-1},
    {10,4,9,6,4,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,10,6,4,9,10,0,8,3,-1,-1,-1,-1,-1,-1,-1},
    {10,0,1,10,6,0,6,4,0,-1,-1,-1,-1,-1,-1,-1},
    {8,3,1,8,1,6,8,6,4,6,1,10,-1,-1,-1,-1},
    {1,4,9,1,2,4,2,6,4,-1,-1,-1,-1,-1,-1,-1},
    {3,0,8,1,2,9,2,4,9,2,6,4,-1,-1,-1,-1},
    {0,2,4,4,2,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {8,3,2,8,2,4,4,2,6,-1,-1,-1,-1,-1,-1,-1},
    {10,4,9,10,6,4,11,2,3,-1,-1,-1,-1,-1,-1,-1},
    {0,8,2,2,8,11,4,9,10,4,10,6,-1,-1,-1,-1},
    {3,11,2,0,1,6,0,6,4,6,1,10,-1,-1,-1,-1},
    {6,4,1,6,1,10,4,8,1,2,1,11,8,11,1,-1},
    {9,6,4,9,3,6,9,1,3,11,6,3,-1,-1,-1,-1},
    {8,11,1,8,1,0,11,6,1,9,1,4,6,4,1,-1},
    {3,11,6,3,6,0,0,6,4,-1,-1,-1,-1,-1,-1,-1},
    {6,4,8,11,6,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {7,10,6,7,8,10,8,9,10,-1,-1,-1,-1,-1,-1,-1},
    {0,7,3,0,10,7,0,9,10,6,7,10,-1,-1,-1,-1},
    {10,6,7,1,10,7,1,7,8,1,8,0,-1,-1,-1,-1},
    {10,6,7,10,7,1,1,7,3,-1,-1,-1,-1,-1,-1,-1},
    {1,2,6,1,6,8,1,8,9,8,6,7,-1,-1,-1,-1},
    {2,6,9,2,9,1,6,7,9,0,9,3,7,3,9,-1},
    {7,8,0,7,0,6,6,0,2,-1,-1,-1,-1,-1,-1,-1},
    {7,3,2,6,7,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {2,3,11,10,6,8,10,8,9,8,6,7,-1,-1,-1,-1},
    {2,0,7,2,7,11,0,9,7,6,7,10,9,10,7,-1},
    {1,8,0,1,7,8,1,10,7,6,7,10,2,3,11,-1},
    {11,2,1,11,1,7,10,6,1,6,7,1,-1,-1,-1,-1},
    {8,9,6,8,6,7,9,1,6,11,6,3,1,3,6,-1},
    {0,9,1,11,6,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {7,8,0,7,0,6,3,11,0,11,6,0,-1,-1,-1,-1},
    {7,11,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {7,6,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {3,0,8,11,7,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,1,9,11,7,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {8,1,9,8,3,1,11,7,6,-1,-1,-1,-1,-1,-1,-1},
    {10,1,2,6,11,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,2,10,3,0,8,6,11,7,-1,-1,-1,-1,-1,-1,-1},
    {2,9,0,2,10,9,6,11,7,-1,-1,-1,-1,-1,-1,-1},
    {6,11,7,2,10,3,10,8,3,10,9,8,-1,-1,-1,-1},
    {7,2,3,6,2,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {7,0,8,7,6,0,6,2,0,-1,-1,-1,-1,-1,-1,-1},
    {2,7,6,2,3,7,0,1,9,-1,-1,-1,-1,-1,-1,-1},
    {1,6,2,1,8,6,1,9,8,8,7,6,-1,-1,-1,-1},
    {10,7,6,10,1,7,1,3,7,-1,-1,-1,-1,-1,-1,-1},
    {10,7,6,1,7,10,1,8,7,1,0,8,-1,-1,-1,-1},
    {0,3,7,0,7,10,0,10,9,6,10,7,-1,-1,-1,-1},
    {7,6,10,7,10,8,8,10,9,-1,-1,-1,-1,-1,-1,-1},
    {6,8,4,11,8,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {3,6,11,3,0,6,0,4,6,-1,-1,-1,-1,-1,-1,-1},
    {8,6,11,8,4,6,9,0,1,-1,-1,-1,-1,-1,-1,-1},
    {9,4,6,9,6,3,9,3,1,11,3,6,-1,-1,-1,-1},
    {6,8,4,6,11,8,2,10,1,-1,-1,-1,-1,-1,-1,-1},
    {1,2,10,3,0,11,0,6,11,0,4,6,-1,-1,-1,-1},
    {4,11,8,4,6,11,0,2,9,2,10,9,-1,-1,-1,-1},
    {10,9,3,10,3,2,9,4,3,11,3,6,4,6,3,-1},
    {8,2,3,8,4,2,4,6,2,-1,-1,-1,-1,-1,-1,-1},
    {0,4,2,4,6,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,9,0,2,3,4,2,4,6,4,3,8,-1,-1,-1,-1},
    {1,9,4,1,4,2,2,4,6,-1,-1,-1,-1,-1,-1,-1},
    {8,1,3,8,6,1,8,4,6,6,10,1,-1,-1,-1,-1},
    {10,1,0,10,0,6,6,0,4,-1,-1,-1,-1,-1,-1,-1},
    {4,6,3,4,3,8,6,10,3,0,3,9,10,9,3,-1},
    {10,9,4,6,10,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,9,5,7,6,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,8,3,4,9,5,11,7,6,-1,-1,-1,-1,-1,-1,-1},
    {5,0,1,5,4,0,7,6,11,-1,-1,-1,-1,-1,-1,-1},
    {11,7,6,8,3,4,3,5,4,3,1,5,-1,-1,-1,-1},
    {9,5,4,10,1,2,7,6,11,-1,-1,-1,-1,-1,-1,-1},
    {6,11,7,1,2,10,0,8,3,4,9,5,-1,-1,-1,-1},
    {7,6,11,5,4,10,4,2,10,4,0,2,-1,-1,-1,-1},
    {3,4,8,3,5,4,3,2,5,10,5,2,11,7,6,-1},
    {7,2,3,7,6,2,5,4,9,-1,-1,-1,-1,-1,-1,-1},
    {9,5,4,0,8,6,0,6,2,6,8,7,-1,-1,-1,-1},
    {3,6,2,3,7,6,1,5,0,5,4,0,-1,-1,-1,-1},
    {6,2,8,6,8,7,2,1,8,4,8,5,1,5,8,-1},
    {9,5,4,10,1,6,1,7,6,1,3,7,-1,-1,-1,-1},
    {1,6,10,1,7,6,1,0,7,8,7,0,9,5,4,-1},
    {4,0,10,4,10,5,0,3,10,6,10,7,3,7,10,-1},
    {7,6,10,7,10,8,5,4,10,4,8,10,-1,-1,-1,-1},
    {6,9,5,6,11,9,11,8,9,-1,-1,-1,-1,-1,-1,-1},
    {3,6,11,0,6,3,0,5,6,0,9,5,-1,-1,-1,-1},
    {0,11,8,0,5,11,0,1,5,5,6,11,-1,-1,-1,-1},
    {6,11,3,6,3,5,5,3,1,-1,-1,-1,-1,-1,-1,-1},
    {1,2,10,9,5,11,9,11,8,11,5,6,-1,-1,-1,-1},
    {0,11,3,0,6,11,0,9,6,5,6,9,1,2,10,-1},
    {11,8,5,11,5,6,8,0,5,10,5,2,0,2,5,-1},
    {6,11,3,6,3,5,2,10,3,10,5,3,-1,-1,-1,-1},
    {5,8,9,5,2,8,5,6,2,3,8,2,-1,-1,-1,-1},
    {9,5,6,9,6,0,0,6,2,-1,-1,-1,-1,-1,-1,-1},
    {1,5,8,1,8,0,5,6,8,3,8,2,6,2,8,-1},
    {1,5,6,2,1,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,3,6,1,6,10,3,8,6,5,6,9,8,9,6,-1},
    {10,1,0,10,0,6,9,5,0,5,6,0,-1,-1,-1,-1},
    {0,3,8,5,6,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {10,5,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {11,5,10,7,5,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {11,5,10,11,7,5,8,3,0,-1,-1,-1,-1,-1,-1,-1},
    {5,11,7,5,10,11,1,9,0,-1,-1,-1,-1,-1,-1,-1},
    {10,7,5,10,11,7,9,8,1,8,3,1,-1,-1,-1,-1},
    {11,1,2,11,7,1,7,5,1,-1,-1,-1,-1,-1,-1,-1},
    {0,8,3,1,2,7,1,7,5,7,2,11,-1,-1,-1,-1},
    {9,7,5,9,2,7,9,0,2,2,11,7,-1,-1,-1,-1},
    {7,5,2,7,2,11,5,9,2,3,2,8,9,8,2,-1},
    {2,5,10,2,3,5,3,7,5,-1,-1,-1,-1,-1,-1,-1},
    {8,2,0,8,5,2,8,7,5,10,2,5,-1,-1,-1,-1},
    {9,0,1,5,10,3,5,3,7,3,10,2,-1,-1,-1,-1},
    {9,8,2,9,2,1,8,7,2,10,2,5,7,5,2,-1},
    {1,3,5,3,7,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,8,7,0,7,1,1,7,5,-1,-1,-1,-1,-1,-1,-1},
    {9,0,3,9,3,5,5,3,7,-1,-1,-1,-1,-1,-1,-1},
    {9,8,7,5,9,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {5,8,4,5,10,8,10,11,8,-1,-1,-1,-1,-1,-1,-1},
    {5,0,4,5,11,0,5,10,11,11,3,0,-1,-1,-1,-1},
    {0,1,9,8,4,10,8,10,11,10,4,5,-1,-1,-1,-1},
    {10,11,4,10,4,5,11,3,4,9,4,1,3,1,4,-1},
    {2,5,1,2,8,5,2,11,8,4,5,8,-1,-1,-1,-1},
    {0,4,11,0,11,3,4,5,11,2,11,1,5,1,11,-1},
    {0,2,5,0,5,9,2,11,5,4,5,8,11,8,5,-1},
    {9,4,5,2,11,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {2,5,10,3,5,2,3,4,5,3,8,4,-1,-1,-1,-1},
    {5,10,2,5,2,4,4,2,0,-1,-1,-1,-1,-1,-1,-1},
    {3,10,2,3,5,10,3,8,5,4,5,8,0,1,9,-1},
    {5,10,2,5,2,4,1,9,2,9,4,2,-1,-1,-1,-1},
    {8,4,5,8,5,3,3,5,1,-1,-1,-1,-1,-1,-1,-1},
    {0,4,5,1,0,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {8,4,5,8,5,3,9,0,5,0,3,5,-1,-1,-1,-1},
    {9,4,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,11,7,4,9,11,9,10,11,-1,-1,-1,-1,-1,-1,-1},
    {0,8,3,4,9,7,9,11,7,9,10,11,-1,-1,-1,-1},
    {1,10,11,1,11,4,1,4,0,7,4,11,-1,-1,-1,-1},
    {3,1,4,3,4,8,1,10,4,7,4,11,10,11,4,-1},
    {4,11,7,9,11,4,9,2,11,9,1,2,-1,-1,-1,-1},
    {9,7,4,9,11,7,9,1,11,2,11,1,0,8,3,-1},
    {11,7,4,11,4,2,2,4,0,-1,-1,-1,-1,-1,-1,-1},
    {11,7,4,11,4,2,8,3,4,3,2,4,-1,-1,-1,-1},
    {2,9,10,2,7,9,2,3,7,7,4,9,-1,-1,-1,-1},
    {9,10,7,9,7,4,10,2,7,8,7,0,2,0,7,-1},
    {3,7,10,3,10,2,7,4,10,1,10,0,4,0,10,-1},
    {1,10,2,8,7,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,9,1,4,1,7,7,1,3,-1,-1,-1,-1,-1,-1,-1},
    {4,9,1,4,1,7,0,8,1,8,7,1,-1,-1,-1,-1},
    {4,0,3,7,4,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,8,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {9,10,8,10,11,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {3,0,9,3,9,11,11,9,10,-1,-1,-1,-1,-1,-1,-1},
    {0,1,10,0,10,8,8,10,11,-1,-1,-1,-1,-1,-1,-1},
    {3,1,10,11,3,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,2,11,1,11,9,9,11,8,-1,-1,-1,-1,-1,-1,-1},
    {3,0,9,3,9,11,1,2,9,2,11,9,-1,-1,-1,-1},
    {0,2,11,8,0,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {3,2,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {2,3,8,2,8,10,10,8,9,-1,-1,-1,-1,-1,-1,-1},
    {9,10,2,0,9,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {2,3,8,2,8,10,0,1,8,1,10,8,-1,-1,-1,-1},
    {1,10,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,3,8,9,1,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,9,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,3,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}
};

// ============================================================================
// SDF noise functions (ported from Unity compute shader)
// ============================================================================

static glm::vec3 hash3(glm::vec2 p) {
    glm::vec3 q(glm::dot(p, glm::vec2(127.1f, 311.7f)),
                glm::dot(p, glm::vec2(269.5f, 183.3f)),
                glm::dot(p, glm::vec2(419.2f, 371.9f)));
    return glm::fract(glm::sin(q) * 43758.5453f);
}

static glm::vec3 noise3(glm::vec2 p) {
    glm::vec2 ip = glm::floor(p);
    glm::vec2 u = glm::fract(p);
    u = u * u * (3.0f - 2.0f * u);
    glm::vec3 res = glm::mix(
        glm::mix(hash3(ip), hash3(ip + glm::vec2(1, 0)), u.x),
        glm::mix(hash3(ip + glm::vec2(0, 1)), hash3(ip + glm::vec2(1, 1)), u.x),
        u.y);
    return res * res;
}

static glm::vec3 fbm(glm::vec2 x) {
    glm::vec3 v(0.0f);
    glm::vec3 a(0.5f);
    for (int i = 0; i < 6; ++i) {
        v += a * noise3(x);
        float c = 0.87f, s = 0.48f;
        glm::vec2 nx(c * x.x + s * x.y, -s * x.x + c * x.y);
        x = nx * 2.0f + glm::vec2(100.0f);
        a *= 0.5f;
    }
    return v;
}

static float rock_sdf(glm::vec3 p, int steps, float seed, float smoothness) {
    float d = glm::length(p) - 0.95f;
    for (int i = 0; i < steps; i++) {
        float j = static_cast<float>(i) + seed;
        float r = 2.5f + glm::fract(std::sin(j * 727.1f) * 435.545f);
        glm::vec3 v = glm::normalize(
            glm::fract(glm::sin(glm::vec3(127.231f, 491.7f, 718.423f) * j) * 435.543f) * 2.0f - 1.0f);
        float a = d;
        float b = glm::length(p + v * r) - r * 0.8f;
        float k = smoothness;
        float h = glm::clamp(0.5f + 0.5f * (-b - a) / k, 0.0f, 1.0f);
        d = glm::mix(a, -b, h) + k * h * (1.0f - h);
    }
    return d;
}

static float scalar_field(glm::vec3 p, const RockParams& params) {
    // Clip bottom for flat base (just below center)
    if (p.y < -0.05f) return 0.0f;

    float d = rock_sdf(p * 2.0f, params.steps, static_cast<float>(params.seed), params.smoothness);
    if (params.displacement_scale > 0.0f) {
        glm::vec2 eps(0.0001f, 0.0f);
        glm::vec3 n;
        n.x = rock_sdf((p + glm::vec3(eps.x, eps.y, eps.y)) * 2.0f, params.steps, static_cast<float>(params.seed), params.smoothness)
            - rock_sdf((p - glm::vec3(eps.x, eps.y, eps.y)) * 2.0f, params.steps, static_cast<float>(params.seed), params.smoothness);
        n.y = rock_sdf((p + glm::vec3(eps.y, eps.x, eps.y)) * 2.0f, params.steps, static_cast<float>(params.seed), params.smoothness)
            - rock_sdf((p - glm::vec3(eps.y, eps.x, eps.y)) * 2.0f, params.steps, static_cast<float>(params.seed), params.smoothness);
        n.z = rock_sdf((p + glm::vec3(eps.y, eps.y, eps.x)) * 2.0f, params.steps, static_cast<float>(params.seed), params.smoothness)
            - rock_sdf((p - glm::vec3(eps.y, eps.y, eps.x)) * 2.0f, params.steps, static_cast<float>(params.seed), params.smoothness);
        n = glm::normalize(n);
        n = glm::max(n * n, glm::vec3(0.001f));
        n /= (n.x + n.y + n.z);
        glm::vec3 disp = fbm(params.displacement_spread * glm::vec2(p.y, p.z)) * n.x
                       + fbm(params.displacement_spread * glm::vec2(p.z, p.x)) * n.y
                       + fbm(params.displacement_spread * glm::vec2(p.x, p.y)) * n.z;
        d += params.displacement_scale * glm::length(disp);
    }
    float t = (0.05f - d) / 0.05f;
    return glm::clamp(t, 0.0f, 1.0f);
}

// ============================================================================
// Marching cubes mesh extraction
// ============================================================================

static glm::vec3 interpolate_vertex(const glm::vec3& p1, const glm::vec3& p2, float v1, float v2) {
    if (std::abs(v1 - v2) < 1e-6f) return p1;
    float t = (0.5f - v1) / (v2 - v1);
    return p1 + t * (p2 - p1);
}

// ============================================================================
// Public API
// ============================================================================

std::unique_ptr<Model> RockGenerator::generate(const RockParams& params) {
    int res = params.resolution;
    float grid_extent = 0.7f;  // larger than SDF radius to avoid boundary clipping
    float grid_size = grid_extent * 2.0f;
    float inv_res = grid_size / static_cast<float>(res);

    // Evaluate scalar field on 3D grid covering [-grid_extent, grid_extent]
    std::vector<float> field(static_cast<size_t>((res + 1) * (res + 1)) * (res + 1));
    auto idx = [&](int x, int y, int z) { return x + (res + 1) * (y + (res + 1) * z); };

    for (int z = 0; z <= res; z++)
    for (int y = 0; y <= res; y++)
    for (int x = 0; x <= res; x++) {
        glm::vec3 p(static_cast<float>(x) * inv_res - grid_extent,
                    static_cast<float>(y) * inv_res - grid_extent,
                    static_cast<float>(z) * inv_res - grid_extent);
        field[idx(x, y, z)] = scalar_field(p, params);
    }

    // Marching cubes
    std::vector<gpu::Vertex3D> vertices;
    std::vector<uint32_t> indices;
    float threshold = 0.5f;

    for (int z = 0; z < res; z++)
    for (int y = 0; y < res; y++)
    for (int x = 0; x < res; x++) {
        // 8 corners of the cube
        float v[8];
        glm::vec3 p[8];
        for (int i = 0; i < 8; i++) {
            int dx = (i & 1) ? 1 : 0;
            int dy = (i & 2) ? 1 : 0;
            int dz = (i & 4) ? 1 : 0;
            // Standard marching cubes corner ordering
            int cx, cy, cz;
            switch (i) {
                case 0: cx=x;   cy=y;   cz=z;   break;
                case 1: cx=x+1; cy=y;   cz=z;   break;
                case 2: cx=x+1; cy=y+1; cz=z;   break;
                case 3: cx=x;   cy=y+1; cz=z;   break;
                case 4: cx=x;   cy=y;   cz=z+1; break;
                case 5: cx=x+1; cy=y;   cz=z+1; break;
                case 6: cx=x+1; cy=y+1; cz=z+1; break;
                case 7: cx=x;   cy=y+1; cz=z+1; break;
                default: cx=x; cy=y; cz=z; break;
            }
            v[i] = field[idx(cx, cy, cz)];
            p[i] = glm::vec3(static_cast<float>(cx) * inv_res - grid_extent,
                             static_cast<float>(cy) * inv_res - grid_extent,
                             static_cast<float>(cz) * inv_res - grid_extent) * params.scale;
        }

        // Compute cube index
        int cube_index = 0;
        for (int i = 0; i < 8; i++) {
            if (v[i] >= threshold) cube_index |= (1 << i);
        }

        if (edge_table[cube_index] == 0) continue;

        // Interpolate edge vertices
        glm::vec3 edge_verts[12];
        if (edge_table[cube_index] & 1)    edge_verts[0]  = interpolate_vertex(p[0], p[1], v[0], v[1]);
        if (edge_table[cube_index] & 2)    edge_verts[1]  = interpolate_vertex(p[1], p[2], v[1], v[2]);
        if (edge_table[cube_index] & 4)    edge_verts[2]  = interpolate_vertex(p[2], p[3], v[2], v[3]);
        if (edge_table[cube_index] & 8)    edge_verts[3]  = interpolate_vertex(p[3], p[0], v[3], v[0]);
        if (edge_table[cube_index] & 16)   edge_verts[4]  = interpolate_vertex(p[4], p[5], v[4], v[5]);
        if (edge_table[cube_index] & 32)   edge_verts[5]  = interpolate_vertex(p[5], p[6], v[5], v[6]);
        if (edge_table[cube_index] & 64)   edge_verts[6]  = interpolate_vertex(p[6], p[7], v[6], v[7]);
        if (edge_table[cube_index] & 128)  edge_verts[7]  = interpolate_vertex(p[7], p[4], v[7], v[4]);
        if (edge_table[cube_index] & 256)  edge_verts[8]  = interpolate_vertex(p[0], p[4], v[0], v[4]);
        if (edge_table[cube_index] & 512)  edge_verts[9]  = interpolate_vertex(p[1], p[5], v[1], v[5]);
        if (edge_table[cube_index] & 1024) edge_verts[10] = interpolate_vertex(p[2], p[6], v[2], v[6]);
        if (edge_table[cube_index] & 2048) edge_verts[11] = interpolate_vertex(p[3], p[7], v[3], v[7]);

        // Generate triangles with smooth SDF-gradient normals
        for (int i = 0; tri_table[cube_index][i] != -1; i += 3) {
            glm::vec3 tri[3] = {
                edge_verts[tri_table[cube_index][i]],
                edge_verts[tri_table[cube_index][i + 1]],
                edge_verts[tri_table[cube_index][i + 2]]
            };

            // Face normal for UV projection
            glm::vec3 face_n = glm::normalize(glm::cross(tri[1] - tri[0], tri[2] - tri[0]));
            if (std::isnan(face_n.x)) face_n = glm::vec3(0, 1, 0);
            glm::vec3 an = glm::abs(face_n);

            uint32_t base = static_cast<uint32_t>(vertices.size());
            for (int j = 0; j < 3; j++) {
                // Compute smooth normal from SDF gradient at this vertex position
                glm::vec3 vp = tri[j] / params.scale; // back to unit space
                float eps = 0.01f;
                glm::vec3 grad;
                grad.x = scalar_field(vp + glm::vec3(eps, 0, 0), params) - scalar_field(vp - glm::vec3(eps, 0, 0), params);
                grad.y = scalar_field(vp + glm::vec3(0, eps, 0), params) - scalar_field(vp - glm::vec3(0, eps, 0), params);
                grad.z = scalar_field(vp + glm::vec3(0, 0, eps), params) - scalar_field(vp - glm::vec3(0, 0, eps), params);
                glm::vec3 normal = glm::length(grad) > 1e-6f ? glm::normalize(grad) : face_n;

                glm::vec3 np = tri[j] / params.scale + 0.5f;
                glm::vec2 uv;
                if (an.x > an.y && an.x > an.z) uv = {np.z, np.y};
                else if (an.y > an.x && an.y > an.z) uv = {np.x, np.z};
                else uv = {np.x, np.y};

                gpu::Vertex3D vert;
                vert.position = tri[j];
                vert.normal = normal;
                vert.texcoord = uv;
                vert.color = params.color;
                vertices.push_back(vert);
            }
            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        }
    }

    if (vertices.empty()) return nullptr;

    // Shift all vertices up so the flat bottom sits at Y=0
    float min_y = 1e9f;
    for (const auto& v : vertices) min_y = std::min(min_y, v.position.y);
    for (auto& v : vertices) v.position.y -= min_y;

    auto model = std::make_unique<Model>();
    Mesh mesh;
    mesh.vertices = std::move(vertices);
    mesh.indices = std::move(indices);
    mesh.has_texture = false;
    mesh.base_color = 0xFF000000 |
        (static_cast<uint32_t>(params.color.r * 255) << 16) |
        (static_cast<uint32_t>(params.color.g * 255) << 8) |
        (static_cast<uint32_t>(params.color.b * 255));
    model->meshes.push_back(std::move(mesh));

    model->min_x = model->min_y = model->min_z = 1e9f;
    model->max_x = model->max_y = model->max_z = -1e9f;
    for (const auto& m : model->meshes) {
        for (const auto& v : m.vertices) {
            model->min_x = std::min(model->min_x, v.position.x);
            model->min_y = std::min(model->min_y, v.position.y);
            model->min_z = std::min(model->min_z, v.position.z);
            model->max_x = std::max(model->max_x, v.position.x);
            model->max_y = std::max(model->max_y, v.position.y);
            model->max_z = std::max(model->max_z, v.position.z);
        }
    }
    // Force symmetric AABB so build_model_transform centering is zero
    // (procedural rocks are already centered at XZ origin)
    float x_extent = std::max(std::abs(model->min_x), std::abs(model->max_x));
    float z_extent = std::max(std::abs(model->min_z), std::abs(model->max_z));
    model->min_x = -x_extent;
    model->max_x = x_extent;
    model->min_z = -z_extent;
    model->max_z = z_extent;
    model->min_y = 0.0f;
    model->compute_bounding_sphere();
    model->loaded = true;

    return model;
}

// ============================================================================
// Presets
// ============================================================================

std::unique_ptr<Model> RockGenerator::generate_boulder(uint32_t seed, const std::string&) {
    RockParams p;
    p.seed = seed == 0 ? 880 : seed;
    p.resolution = 40;
    p.scale = 1.0f;
    p.steps = 20;
    p.smoothness = 0.05f;
    p.displacement_scale = 0.15f;
    p.displacement_spread = 10.0f;
    p.color = {0.5f, 0.48f, 0.45f, 1.0f};
    return generate(p);
}

std::unique_ptr<Model> RockGenerator::generate_slate(uint32_t seed, const std::string&) {
    RockParams p;
    p.seed = seed == 0 ? 123 : seed;
    p.resolution = 40;
    p.scale = 1.0f;
    p.steps = 15;
    p.smoothness = 0.03f;  // sharper edges
    p.displacement_scale = 0.08f;
    p.displacement_spread = 15.0f;
    p.color = {0.42f, 0.4f, 0.38f, 1.0f};  // darker gray
    return generate(p);
}

std::unique_ptr<Model> RockGenerator::generate_spire(uint32_t seed, const std::string&) {
    RockParams p;
    p.seed = seed == 0 ? 456 : seed;
    p.resolution = 40;
    p.scale = 1.0f;
    p.steps = 25;
    p.smoothness = 0.08f;  // smoother blends = taller shapes
    p.displacement_scale = 0.1f;
    p.displacement_spread = 8.0f;
    p.color = {0.55f, 0.52f, 0.48f, 1.0f};  // lighter
    return generate(p);
}

std::unique_ptr<Model> RockGenerator::generate_cluster(uint32_t seed, const std::string&) {
    RockParams p;
    p.seed = seed == 0 ? 789 : seed;
    p.resolution = 40;
    p.scale = 1.0f;
    p.steps = 30;
    p.smoothness = 0.12f;  // very smooth = more merged blobs
    p.displacement_scale = 0.2f;
    p.displacement_spread = 12.0f;
    p.color = {0.48f, 0.45f, 0.42f, 1.0f};
    return generate(p);
}

std::unique_ptr<Model> RockGenerator::generate_mossy(uint32_t seed, const std::string&) {
    RockParams p;
    p.seed = seed == 0 ? 321 : seed;
    p.resolution = 40;
    p.scale = 1.0f;
    p.steps = 18;
    p.smoothness = 0.06f;
    p.displacement_scale = 0.18f;
    p.displacement_spread = 7.0f;
    p.color = {0.35f, 0.42f, 0.32f, 1.0f};  // greenish
    return generate(p);
}

} // namespace mmo::engine::procedural
