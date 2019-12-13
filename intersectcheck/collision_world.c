/** 
 * collision_world.c -- detect and handle line segment intersections
 * Copyright (c) 2012 the Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. 
 **/

#include "./collision_world.h"

#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>

#include "./intersection_detection.h"
#include "./intersection_event_list.h"
#include "./line.h"
#include "./quadtree.h"

#include "./cilktool.h"
#include <cilk/cilk.h>
#include <cilk/reducer.h>

typedef CILK_C_DECLARE_REDUCER(IntersectionEventList) IntersectionEventListReducer;
IntersectionEventListReducer X = CILK_C_INIT_REDUCER(IntersectionEventList,           // type
  intersection_event_list_reduce, intersection_event_list_identity, intersection_event_list_destroy, // functions
  (IntersectionEventList) { .head = NULL, .tail = NULL });     // initial value

CollisionWorld* CollisionWorld_new(const unsigned int capacity) {
  assert(capacity > 0);

  CollisionWorld* collisionWorld = malloc(sizeof(CollisionWorld));
  if (collisionWorld == NULL) {
    return NULL;
  }

  collisionWorld->numLineWallCollisions = 0;
  collisionWorld->numLineLineCollisions = 0;
  collisionWorld->timeStep = 0.5;
  collisionWorld->lines = malloc(capacity * sizeof(Line*));
  collisionWorld->numOfLines = 0;
  return collisionWorld;
}

void CollisionWorld_delete(CollisionWorld* collisionWorld) {
  for (int i = 0; i < collisionWorld->numOfLines; i++) {
    free(collisionWorld->lines[i]);
  }
  free(collisionWorld->lines);
  free(collisionWorld);
}

unsigned int CollisionWorld_getNumOfLines(CollisionWorld* collisionWorld) {
  return collisionWorld->numOfLines;
}

void CollisionWorld_addLine(CollisionWorld* collisionWorld, Line *line) {
  collisionWorld->lines[collisionWorld->numOfLines] = line;
  line->distance = Vec_multiply(line->velocity, collisionWorld->timeStep);
  line->vp1 = Vec_add(line->p1, line->distance);
  line->vp2 = Vec_add(line->p2, line->distance);
  line->tl.x = MIN(line->p1.x, line->p2.x) + MIN(0, line->velocity.x);
  line->tl.y = MIN(line->p1.y, line->p2.y) + MIN(0, line->velocity.y);
  line->br.x = MAX(line->p1.x, line->p2.x) + MAX(0, line->velocity.x);
  line->br.y = MAX(line->p1.y, line->p2.y) + MAX(0, line->velocity.y);
  collisionWorld->numOfLines++;
}

Line* CollisionWorld_getLine(CollisionWorld* collisionWorld,
                             const unsigned int index) {
  if (index >= collisionWorld->numOfLines) {
    return NULL;
  }
  return collisionWorld->lines[index];
}

void CollisionWorld_updateLines(CollisionWorld* collisionWorld) {
#ifdef REF
  CILK_C_REGISTER_REDUCER(X);
  CollisionWorld_detectIntersection(collisionWorld, &REDUCER_VIEW(X));
  CILK_C_UNREGISTER_REDUCER(X);
#else
  CILK_C_REGISTER_REDUCER(X);
  CollisionWorld_detectIntersectionQuadtree(collisionWorld, &REDUCER_VIEW(X));
  CILK_C_UNREGISTER_REDUCER(X);
#endif
  CollisionWorld_updatePosition(collisionWorld);
  CollisionWorld_lineWallCollision(collisionWorld);
}

void CollisionWorld_updatePosition(CollisionWorld* collisionWorld) {
  for (int i = 0; i < collisionWorld->numOfLines; i++) {
    Line *line = collisionWorld->lines[i];
    line->p1 = line->vp1;
    line->p2 = line->vp2;
    line->vp1 = Vec_add(line->p1, line->distance);
    line->vp2 = Vec_add(line->p2, line->distance);
    line->tl.x = MIN(line->p1.x, line->p2.x) + MIN(0, line->velocity.x);
    line->br.y = MAX(line->p1.y, line->p2.y) + MAX(0, line->velocity.y);
    line->tl.y = MIN(line->p1.y, line->p2.y) + MIN(0, line->velocity.y);
    line->br.x = MAX(line->p1.x, line->p2.x) + MAX(0, line->velocity.x);
  }
}

void CollisionWorld_lineWallCollision(CollisionWorld* collisionWorld) {
  for (int i = 0; i < collisionWorld->numOfLines; i++) {
    Line *line = collisionWorld->lines[i];
    bool collide = false;

    // Right side
    if ((line->p1.x > BOX_XMAX || line->p2.x > BOX_XMAX)
        && (line->velocity.x > 0)) {
      line->velocity.x = -line->velocity.x;
      collide = true;
    }
    // Left side
    if ((line->p1.x < BOX_XMIN || line->p2.x < BOX_XMIN)
        && (line->velocity.x < 0)) {
      line->velocity.x = -line->velocity.x;
      collide = true;
    }
    // Top side
    if ((line->p1.y > BOX_YMAX || line->p2.y > BOX_YMAX)
        && (line->velocity.y > 0)) {
      line->velocity.y = -line->velocity.y;
      collide = true;
    }
    // Bottom side
    if ((line->p1.y < BOX_YMIN || line->p2.y < BOX_YMIN)
        && (line->velocity.y < 0)) {
      line->velocity.y = -line->velocity.y;
      collide = true;
    }
    // Update total number of collisions.
    if (collide == true) {
      collisionWorld->numLineWallCollisions++;
      line->distance = Vec_multiply(line->velocity, collisionWorld->timeStep);
      line->vp1 = Vec_add(line->p1, line->distance);
      line->vp2 = Vec_add(line->p2, line->distance);
      line->tl.x = MIN(line->p1.x, line->p2.x) + MIN(0, line->distance.x);
      line->br.y = MAX(line->p1.y, line->p2.y) + MAX(0, line->distance.y);
      line->tl.y = MIN(line->p1.y, line->p2.y) + MIN(0, line->distance.y);
      line->br.x = MAX(line->p1.x, line->p2.x) + MAX(0, line->distance.x);
    }
  }
}

void CollisionWorld_detectIntersectionQuadtree(CollisionWorld* collisionWorld,
  IntersectionEventList* intersectionEventList) {
  Node* root = NewQuadtree(collisionWorld->lines, collisionWorld->numOfLines);
  Node_detectIntersection(root, collisionWorld, &REDUCER_VIEW(X));

  assert(collisionWorld->numOfLines == countLines(root));

  intersectionEventList = &X.value;
  collisionWorld->numLineLineCollisions += intersectionEventList->lineLineCollisions;

  // Sort the intersection event list.
  IntersectionEventNode* startNode = intersectionEventList->head;
  while (startNode != NULL) {
    IntersectionEventNode* minNode = startNode;
    IntersectionEventNode* curNode = startNode->next;
    while (curNode != NULL) {
      if (IntersectionEventNode_compareData(curNode, minNode) < 0) {
        minNode = curNode;
      }
      curNode = curNode->next;
    }
    if (minNode != startNode) {
      IntersectionEventNode_swapData(minNode, startNode);
    }
    startNode = startNode->next;
  }

  // Call the collision solver for each intersection event.
  IntersectionEventNode* curNode = intersectionEventList->head;

  while (curNode != NULL) {
    CollisionWorld_collisionSolver(collisionWorld, curNode->l1, curNode->l2,
                                   curNode->intersectionType);
    curNode = curNode->next;
  }

  IntersectionEventList_deleteNodes(&REDUCER_VIEW(X));
  FreeNode(root);
}

void Node_detectIntersection(Node* node, CollisionWorld* collisionWorld,
  IntersectionEventList* intersectionEventList) {
  // Test all line-line pairs in node and parent nodes to see if they will intersect before the
  // next time step.
  cilk_for (int i = 0; i < node->num_of_lines; i++) {
    Line* l1 = node->lines[i];
    CheckContainedLines(node, i, l1, &REDUCER_VIEW(X), collisionWorld);
    for (Node* parent = node->parent; parent != NULL; parent = parent->parent) {
      for (int j = 0; j < parent->num_of_lines; j++) {
        Line* l2 = parent->lines[j];

        if (rectangleIntersection(l1->tl, l1->br, l2->tl, l2->br)) {
          // intersect expects compareLines(l1, l2) < 0 to be true.
          // Swap l1 and l2, if necessary.
          IntersectionType intersectionType;
          if (compareLines(l1, l2) >= 0) {
            intersectionType = intersect(l2, l1, collisionWorld->timeStep);
            if (intersectionType != NO_INTERSECTION) {
              IntersectionEventList_appendNode(&REDUCER_VIEW(X), l2, l1,
                                              intersectionType);
            }
          } else {
            intersectionType = intersect(l1, l2, collisionWorld->timeStep);
            if (intersectionType != NO_INTERSECTION) {
              IntersectionEventList_appendNode(&REDUCER_VIEW(X), l1, l2,
                                              intersectionType);
            }
          }
        }
      }
    }
  }

  if (node->top_left != NULL) {
    cilk_spawn Node_detectIntersection(node->top_left, collisionWorld, &REDUCER_VIEW(X));
    cilk_spawn Node_detectIntersection(node->top_right, collisionWorld, &REDUCER_VIEW(X));
    cilk_spawn Node_detectIntersection(node->bottom_left, collisionWorld, &REDUCER_VIEW(X));
    Node_detectIntersection(node->bottom_right, collisionWorld, &REDUCER_VIEW(X));
    cilk_sync;
  }
}

void CheckContainedLines(Node* node, int i, Line* l1, IntersectionEventList* intersectionEventList, CollisionWorld* collisionWorld) {
  for (int j = i + 1; j < node->num_of_lines; j++) {
    Line* l2 = node->lines[j];
    
    if (rectangleIntersection(l1->tl, l1->br, l2->tl, l2->br)) {
      IntersectionType intersectionType;
      if (compareLines(l1, l2) >= 0) {
        intersectionType = intersect(l2, l1, collisionWorld->timeStep);
        if (intersectionType != NO_INTERSECTION) {
          IntersectionEventList_appendNode(&REDUCER_VIEW(X), l2, l1,
                                          intersectionType);
        }
      } else {
        intersectionType = intersect(l1, l2, collisionWorld->timeStep);
        if (intersectionType != NO_INTERSECTION) {
          IntersectionEventList_appendNode(&REDUCER_VIEW(X), l1, l2,
                                          intersectionType);
        }
      }
    }
  }
}

void CollisionWorld_detectIntersection(CollisionWorld* collisionWorld, IntersectionEventList* intersectionEventList) {
  // Test all line-line pairs to see if they will intersect before the
  // next time step.
  cilk_for (int i = 0; i < collisionWorld->numOfLines; i++) {
    Line *l1 = collisionWorld->lines[i];

    for (int j = i + 1; j < collisionWorld->numOfLines; j++) {
      Line *l2 = collisionWorld->lines[j];
      
      // intersect expects compareLines(l1, l2) < 0 to be true.
      // Swap l1 and l2, if necessary.
      if (compareLines(l1, l2) >= 0) {
        Line *temp = l1;
        l1 = l2;
        l2 = temp;
      }

      IntersectionType intersectionType =
          intersect(l1, l2, collisionWorld->timeStep);
      if (intersectionType != NO_INTERSECTION) {
        IntersectionEventList_appendNode(&REDUCER_VIEW(X), l1, l2,
                                        intersectionType);
        collisionWorld->numLineLineCollisions++;
      }
    }
  }

  // Sort the intersection event list.
  intersectionEventList = &X.value;
  collisionWorld->numLineLineCollisions += intersectionEventList->lineLineCollisions;
  IntersectionEventNode* startNode = intersectionEventList->head;
  while (startNode != NULL) {
    IntersectionEventNode* minNode = startNode;
    IntersectionEventNode* curNode = startNode->next;
    while (curNode != NULL) {
      if (IntersectionEventNode_compareData(curNode, minNode) < 0) {
        minNode = curNode;
      }
      curNode = curNode->next;
    }
    if (minNode != startNode) {
      IntersectionEventNode_swapData(minNode, startNode);
    }
    startNode = startNode->next;
  }

  // Call the collision solver for each intersection event.
  IntersectionEventNode* curNode = intersectionEventList->head;

  while (curNode != NULL) {
    CollisionWorld_collisionSolver(collisionWorld, curNode->l1, curNode->l2,
                                   curNode->intersectionType);
    curNode = curNode->next;
  }

  IntersectionEventList_deleteNodes(intersectionEventList);
}

unsigned int CollisionWorld_getNumLineWallCollisions(
    CollisionWorld* collisionWorld) {
  return collisionWorld->numLineWallCollisions;
}

unsigned int CollisionWorld_getNumLineLineCollisions(
    CollisionWorld* collisionWorld) {
  return collisionWorld->numLineLineCollisions;
}

void CollisionWorld_collisionSolver(CollisionWorld* collisionWorld,
                                    Line *l1, Line *l2,
                                    IntersectionType intersectionType) {
  assert(compareLines(l1, l2) < 0);
  assert(intersectionType == L1_WITH_L2
         || intersectionType == L2_WITH_L1
         || intersectionType == ALREADY_INTERSECTED);

  // Despite our efforts to determine whether lines will intersect ahead
  // of time (and to modify their velocities appropriately), our
  // simplified model can sometimes cause lines to intersect.  In such a
  // case, we compute velocities so that the two lines can get unstuck in
  // the fastest possible way, while still conserving momentum and kinetic
  // energy.
  if (intersectionType == ALREADY_INTERSECTED) {
    Vec p = getIntersectionPoint(l1->p1, l1->p2, l2->p1, l2->p2);

    if (Vec_length(Vec_subtract(l1->p1, p))
        < Vec_length(Vec_subtract(l1->p2, p))) {
      l1->velocity = Vec_multiply(Vec_normalize(Vec_subtract(l1->p2, p)),
                                  Vec_length(l1->velocity));
    } else {
      l1->velocity = Vec_multiply(Vec_normalize(Vec_subtract(l1->p1, p)),
                                  Vec_length(l1->velocity));
    }
    if (Vec_length(Vec_subtract(l2->p1, p))
        < Vec_length(Vec_subtract(l2->p2, p))) {
      l2->velocity = Vec_multiply(Vec_normalize(Vec_subtract(l2->p2, p)),
                                  Vec_length(l2->velocity));
    } else {
      l2->velocity = Vec_multiply(Vec_normalize(Vec_subtract(l2->p1, p)),
                                  Vec_length(l2->velocity));
    }

    l1->distance = Vec_multiply(l1->velocity, collisionWorld->timeStep);
    l1->vp1 = Vec_add(l1->p1, l1->distance);
    l1->vp2 = Vec_add(l1->p2, l1->distance);

    l1->tl.x = MIN(l1->p1.x, l1->p2.x) + MIN(0, l1->distance.x);
    l1->tl.y = MIN(l1->p1.y, l1->p2.y) + MIN(0, l1->distance.y);
    l1->br.x = MAX(l1->p1.x, l1->p2.x) + MAX(0, l1->distance.x);
    l1->br.y = MAX(l1->p1.y, l1->p2.y) + MAX(0, l1->distance.y);

    l2->distance = Vec_multiply(l2->velocity, collisionWorld->timeStep);
    l2->vp1 = Vec_add(l2->p1, l2->distance);
    l2->vp2 = Vec_add(l2->p2, l2->distance);

    l2->tl.x = MIN(l2->p1.x, l2->p2.x) + MIN(0, l2->distance.x);
    l2->tl.y = MIN(l2->p1.y, l2->p2.y) + MIN(0, l2->distance.y);
    l2->br.x = MAX(l2->p1.x, l2->p2.x) + MAX(0, l2->distance.x);
    l2->br.y = MAX(l2->p1.y, l2->p2.y) + MAX(0, l2->distance.y);
    return;
  }

  // Compute the collision face/normal vectors.
  Vec face;
  Vec normal;
  if (intersectionType == L1_WITH_L2) {
    Vec v = Vec_makeFromLine(*l2);
    face = Vec_normalize(v);
  } else {
    Vec v = Vec_makeFromLine(*l1);
    face = Vec_normalize(v);
  }
  normal = Vec_orthogonal(face);

  // Obtain each line's velocity components with respect to the collision
  // face/normal vectors.
  double v1Face = Vec_dotProduct(l1->velocity, face);
  double v2Face = Vec_dotProduct(l2->velocity, face);
  double v1Normal = Vec_dotProduct(l1->velocity, normal);
  double v2Normal = Vec_dotProduct(l2->velocity, normal);

  // Compute the mass of each line (we simply use its length).
  double m1 = Vec_length(Vec_subtract(l1->p1, l1->p2));
  double m2 = Vec_length(Vec_subtract(l2->p1, l2->p2));

  // Perform the collision calculation (computes the new velocities along
  // the direction normal to the collision face such that momentum and
  // kinetic energy are conserved).
  double newV1Normal = ((m1 - m2) / (m1 + m2)) * v1Normal
      + (2 * m2 / (m1 + m2)) * v2Normal;
  double newV2Normal = (2 * m1 / (m1 + m2)) * v1Normal
      + ((m2 - m1) / (m2 + m1)) * v2Normal;

  // Combine the resulting velocities.
  l1->velocity = Vec_add(Vec_multiply(normal, newV1Normal),
                         Vec_multiply(face, v1Face));
  l2->velocity = Vec_add(Vec_multiply(normal, newV2Normal),
                         Vec_multiply(face, v2Face));

  l1->distance = Vec_multiply(l1->velocity, collisionWorld->timeStep);
  l1->vp1 = Vec_add(l1->p1, l1->distance);
  l1->vp2 = Vec_add(l1->p2, l1->distance);

  l1->tl.x = MIN(l1->p1.x, l1->p2.x) + MIN(0, l1->distance.x);
  l1->tl.y = MIN(l1->p1.y, l1->p2.y) + MIN(0, l1->distance.y);
  l1->br.x = MAX(l1->p1.x, l1->p2.x) + MAX(0, l1->distance.x);
  l1->br.y = MAX(l1->p1.y, l1->p2.y) + MAX(0, l1->distance.y);

  l2->distance = Vec_multiply(l2->velocity, collisionWorld->timeStep);
  l2->vp1 = Vec_add(l2->p1, l2->distance);
  l2->vp2 = Vec_add(l2->p2, l2->distance);

  l2->tl.x = MIN(l2->p1.x, l2->p2.x) + MIN(0, l2->distance.x);
  l2->tl.y = MIN(l2->p1.y, l2->p2.y) + MIN(0, l2->distance.y);
  l2->br.x = MAX(l2->p1.x, l2->p2.x) + MAX(0, l2->distance.x);
  l2->br.y = MAX(l2->p1.y, l2->p2.y) + MAX(0, l2->distance.y);

  return;
}
