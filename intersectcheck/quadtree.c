/**
 * Methods for the quadtree.
**/
#include "./quadtree.h"

#include <assert.h>

#include "./line.h"
#include "./vec.h"

#include <cilk/cilk.h>
#include <cilk/reducer.h>

static int R = 100;
static int MAX_DEPTH = 15;

//Returns a new node representing the root of the Quadtree
//Note: Quadtree is NOT an actual structure, we just use this as a label for a set of connected nodes
inline Node* NewQuadtree(Line** lines, int num_lines) {
    Node* root = Node_new(NULL, (double)BOX_XMIN, (double)BOX_XMAX, (double)BOX_YMIN, (double)BOX_YMAX, 0);
    Partition(root, lines, num_lines);
    return root;
}

void Partition(Node* node, Line** lines, int num_lines_in_node) {
    if (num_lines_in_node <= R || node->depth > MAX_DEPTH) {
        node->lines = malloc(num_lines_in_node * sizeof(Line*));
        for (int i = 0; i < num_lines_in_node; i++) {
          node->lines[i] = lines[i];
        }
        node->num_of_lines = num_lines_in_node;
        return;
    } 

    const double mid_width = (node->x2 + node->x1)/2;
    const double mid_height = (node->y2 + node->y1)/2;

    node->top_left = Node_new(node, node->x1, mid_width, node->y1, mid_height, node->depth + 1);
    node->top_right = Node_new(node, mid_width, node->x2, node->y1, mid_height, node->depth + 1);
    node->bottom_left = Node_new(node, node->x1, mid_width, mid_height, node->y2, node->depth + 1);
    node->bottom_right = Node_new(node, mid_width, node->x2, mid_height, node->y2, node->depth + 1);

    Line* quadrant_lines[5][num_lines_in_node];
    int quadrant_counters[5] = {0};

    for (int i = 0; i < num_lines_in_node; i++) {
        int quadrant = getQuadrant(lines[i], mid_width, mid_height);
        quadrant_lines[quadrant][quadrant_counters[quadrant]++] = lines[i];
    }

    node->lines = malloc(quadrant_counters[4] * sizeof(Line*));
    for (int i = 0; i < quadrant_counters[4]; i++) {
        node->lines[i] = quadrant_lines[4][i];
    }
    node->num_of_lines = quadrant_counters[4];

    cilk_spawn Partition(node->top_left, quadrant_lines[1], quadrant_counters[1]);
    cilk_spawn Partition(node->top_right, quadrant_lines[0], quadrant_counters[0]);
    cilk_spawn Partition(node->bottom_left, quadrant_lines[2], quadrant_counters[2]);
    Partition(node->bottom_right, quadrant_lines[3], quadrant_counters[3]);
    cilk_sync;
}

int getQuadrant(Line* line, const double mid_width, const double mid_height) {
    // top left
    if (line->p1.x < mid_width && line->p1.y < mid_height &&
        line->p2.x < mid_width && line->p2.y < mid_height &&
        line->vp1.x < mid_width && line->vp1.y < mid_height &&
        line->vp2.x < mid_width && line->vp2.y < mid_height) {
        return 1;
    }

    // top right
    if (line->p1.x > mid_width && line->p1.y < mid_height &&
        line->p2.x > mid_width && line->p2.y < mid_height &&
        line->vp1.x > mid_width && line->vp1.y < mid_height &&
        line->vp2.x > mid_width && line->vp2.y < mid_height) {
        return 0;
    }

    // bottom left
    if (line->p1.x < mid_width && line->p1.y > mid_height &&
        line->p2.x < mid_width && line->p2.y > mid_height &&
        line->vp1.x < mid_width && line->vp1.y > mid_height &&
        line->vp2.x < mid_width && line->vp2.y > mid_height) {
        return 2;
    }

    // bottom right
    if (line->p1.x > mid_width && line->p1.y > mid_height &&
        line->p2.x > mid_width && line->p2.y > mid_height &&
        line->vp1.x > mid_width && line->vp1.y > mid_height &&
        line->vp2.x > mid_width && line->vp2.y > mid_height) {
        return 3;
    }

    return 4;
}

inline Node* Node_new(Node* parent, vec_dimension x1, vec_dimension x2, vec_dimension y1, vec_dimension y2, int depth) {
    Node* new_node = malloc(sizeof(Node));
    new_node->x1 = x1;
    new_node->x2 = x2;
    new_node->y1 = y1;
    new_node->y2 = y2;

    new_node->parent = parent;
    new_node->top_left = NULL;
    new_node->top_right = NULL;
    new_node->bottom_left = NULL;
    new_node->bottom_right = NULL;

    // Set this later in partition() 
    new_node->lines = NULL;
    new_node->num_of_lines = 0;
    new_node->depth = depth;
    return new_node;

}

inline void FreeNode(Node* node){
    if (node->top_left != NULL) {
        FreeNode(node->top_left);
        FreeNode(node->top_right);
        FreeNode(node->bottom_left);
        FreeNode(node->bottom_right);
    }
    free(node->lines);
    free(node);
    node = NULL;
}

int countLines(Node* node) {
  int count = 0;
  if (node->top_left != NULL) {
    count += countLines(node->top_left);
  }

  if (node->top_right != NULL) {
    count += countLines(node->top_right);
  }

  if (node->bottom_left != NULL) {
    count += countLines(node->bottom_left);
  }

  if (node->bottom_right != NULL) {
    count += countLines(node->bottom_right);
  }

  count += node->num_of_lines;
  return count;
}
