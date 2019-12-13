#ifndef QUADTREE_H_
#define QUADTREE_H_

#include "./line.h"
// A quadtree node.
typedef struct Node {
    // sides
    vec_dimension x1; // left side of 2D box
    vec_dimension x2; // right side of 2D box
    vec_dimension y1; // top of 2D box
    vec_dimension y2; // bottom of 2D box

    struct Node* parent; // parent node pointer

    // children
    struct Node* top_left; // top left child
    struct Node* top_right; // top right child
    struct Node* bottom_left; // bottom left child
    struct Node* bottom_right; // bottom right child

    Line** lines; // lines within node
    int num_of_lines;
    int depth;
} Node;

Node* NewQuadtree(Line** lines, int num_lines); // Allocates memory for a new root node
void Partition(Node* node, Line** lines, int num_lines_in_node); // Allocates memory as needed
int getQuadrant(Line* line, const double mid_width, const double mid_height);
int countLines(Node* node);
Node* Node_new(Node* parent, vec_dimension x1, vec_dimension x2, vec_dimension y1, vec_dimension y2, int depth);
void FreeNode(Node* node); // Free memory for node and children

#endif  // QUADTREE_H_