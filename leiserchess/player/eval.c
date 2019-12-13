// Copyright (c) 2015 MIT License by 6.172 Staff

#include "./eval.h"

#include <float.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include "./move_gen.h"
#include "./tbassert.h"

// -----------------------------------------------------------------------------
// Evaluation
// -----------------------------------------------------------------------------

typedef int32_t ev_score_t;  // Static evaluator uses "hi res" values

int RANDOMIZE;

int PCENTRAL;
int PBETWEEN;
int PCENTRAL;
int KFACE;
int KAGGRESSIVE;
int MOBILITY;
int LCOVERAGE;

char blank_laser_map[ARR_SIZE] = {
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  4, 0, 0, 0, 0, 0, 0, 0, 0, 4,
  4, 0, 0, 0, 0, 0, 0, 0, 0, 4,
  4, 0, 0, 0, 0, 0, 0, 0, 0, 4,
  4, 0, 0, 0, 0, 0, 0, 0, 0, 4,
  4, 0, 0, 0, 0, 0, 0, 0, 0, 4,
  4, 0, 0, 0, 0, 0, 0, 0, 0, 4,
  4, 0, 0, 0, 0, 0, 0, 0, 0, 4,
  4, 0, 0, 0, 0, 0, 0, 0, 0, 4,
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4
};

// Heuristics for static evaluation - described in the google doc
// mentioned in the handout.

double pcentral_bonus_lookup[64] = { 0.25, 0.36262256080090182752684313527424819767475128173828125, 0.44098300562505243771482810188899748027324676513671875, 0.4696699141100892926914411873440258204936981201171875,
 0.4696699141100892926914411873440258204936981201171875, 0.44098300562505243771482810188899748027324676513671875, 0.36262256080090182752684313527424819767475128173828125, 0.25,
 0.36262256080090182752684313527424819767475128173828125, 0.49999999999999988897769753748434595763683319091796875, 0.6047152924789525041404658622923307120800018310546875, 0.6464466094067262691424957665731199085712432861328125,
 0.6464466094067262691424957665731199085712432861328125, 0.6047152924789525041404658622923307120800018310546875, 0.49999999999999988897769753748434595763683319091796875, 0.36262256080090182752684313527424819767475128173828125,
 0.44098300562505243771482810188899748027324676513671875, 0.6047152924789525041404658622923307120800018310546875, 0.75, 0.82322330470336313457124788328655995428562164306640625,
 0.82322330470336313457124788328655995428562164306640625, 0.75, 0.6047152924789525041404658622923307120800018310546875, 0.44098300562505243771482810188899748027324676513671875,
 0.4696699141100892926914411873440258204936981201171875, 0.6464466094067262691424957665731199085712432861328125, 0.82322330470336313457124788328655995428562164306640625, 1,
 1, 0.82322330470336313457124788328655995428562164306640625, 0.6464466094067262691424957665731199085712432861328125, 0.4696699141100892926914411873440258204936981201171875,
 0.4696699141100892926914411873440258204936981201171875, 0.6464466094067262691424957665731199085712432861328125, 0.82322330470336313457124788328655995428562164306640625, 1,
 1, 0.82322330470336313457124788328655995428562164306640625, 0.6464466094067262691424957665731199085712432861328125, 0.4696699141100892926914411873440258204936981201171875,
 0.44098300562505243771482810188899748027324676513671875, 0.6047152924789525041404658622923307120800018310546875, 0.75, 0.82322330470336313457124788328655995428562164306640625,
 0.82322330470336313457124788328655995428562164306640625, 0.75, 0.6047152924789525041404658622923307120800018310546875, 0.44098300562505243771482810188899748027324676513671875,
 0.36262256080090182752684313527424819767475128173828125, 0.49999999999999988897769753748434595763683319091796875, 0.6047152924789525041404658622923307120800018310546875, 0.6464466094067262691424957665731199085712432861328125,
 0.6464466094067262691424957665731199085712432861328125, 0.6047152924789525041404658622923307120800018310546875, 0.49999999999999988897769753748434595763683319091796875, 0.36262256080090182752684313527424819767475128173828125,
 0.25, 0.36262256080090182752684313527424819767475128173828125, 0.44098300562505243771482810188899748027324676513671875, 0.4696699141100892926914411873440258204936981201171875,
 0.4696699141100892926914411873440258204936981201171875, 0.44098300562505243771482810188899748027324676513671875, 0.36262256080090182752684313527424819767475128173828125, 0.25,
};
// PCENTRAL heuristic: Bonus for Pawn near center of board
// ev_score_t pcentral_ref(fil_t f, rnk_t r) {
//   double df = BOARD_WIDTH / 2 - f - 1;
//   if (df < 0) {
//     df = f - BOARD_WIDTH / 2;
//   }
//   double dr = BOARD_WIDTH / 2 - r - 1;
//   if (dr < 0) {
//     dr = r - BOARD_WIDTH / 2;
//   }
//   double bonus = 1 - sqrt(df * df + dr * dr) / (BOARD_WIDTH / sqrt(2));
//   return PCENTRAL * bonus;
// }

//Compute pcentral using lookup table
ev_score_t pcentral(fil_t f, rnk_t r) {
  return PCENTRAL * pcentral_bonus_lookup[f*BOARD_WIDTH + r];
}

// returns true if c lies on or between a and b, which are not ordered
bool between(int c, int a, int b) {
  bool x = ((c >= a) && (c <= b)) || ((c <= a) && (c >= b));
  return x;
}

// PBETWEEN heuristic: Bonus for Pawn at (f, r) in rectangle defined by Kings at the corners
ev_score_t pbetween(fil_t f, rnk_t r, fil_t white_fil, rnk_t white_rnk, fil_t black_fil, rnk_t black_rnk) {
  bool is_between =
    between(f, white_fil, black_fil) &&
    between(r, white_rnk, black_rnk);
  return is_between ? PBETWEEN : 0;
}

// KFACE heuristic: bonus (or penalty) for King facing toward the other King
ev_score_t kface(position_t* p, fil_t f, rnk_t r) {
  square_t sq = square_of(f, r);
  piece_t x = p->board[sq];
  color_t c = color_of(x);
  square_t opp_sq = p->kloc[opp_color(c)];
  int delta_fil = fil_of(opp_sq) - f;
  int delta_rnk = rnk_of(opp_sq) - r;
  int bonus;

  switch (ori_of(x)) {
  case NN:
    bonus = delta_rnk;
    break;

  case EE:
    bonus = delta_fil;
    break;

  case SS:
    bonus = -delta_rnk;
    break;

  case WW:
    bonus = -delta_fil;
    break;

  default:
    bonus = 0;
    tbassert(false, "Illegal King orientation.\n");
  }

  return (bonus * KFACE) / (abs(delta_rnk) + abs(delta_fil));
}

// KAGGRESSIVE heuristic: bonus for King with more space to back
ev_score_t kaggressive(position_t* p, fil_t f, rnk_t r) {
  square_t sq = square_of(f, r);
  piece_t x = p->board[sq];
  color_t c = color_of(x);
  tbassert(ptype_of(x) == KING, "ptype_of(x) = %d\n", ptype_of(x));

  square_t opp_sq = p->kloc[opp_color(c)];
  fil_t of = fil_of(opp_sq);
  rnk_t _or = (rnk_t) rnk_of(opp_sq);

  int delta_fil = of - f;
  int delta_rnk = _or - r;

  int bonus = 0;

  if (delta_fil >= 0 && delta_rnk >= 0) {
    bonus = (f + 1) * (r + 1);
  } else if (delta_fil <= 0 && delta_rnk >= 0) {
    bonus = (BOARD_WIDTH - f) * (r + 1);
  } else if (delta_fil <= 0 && delta_rnk <= 0) {
    bonus = (BOARD_WIDTH - f) * (BOARD_WIDTH - r);
  } else if (delta_fil >= 0 && delta_rnk <= 0) {
    bonus = (f + 1) * (BOARD_WIDTH - r);
  }

  return (KAGGRESSIVE * bonus) / (BOARD_WIDTH * BOARD_WIDTH);
}

// Marks the path/line-of-sight of the laser until it hits a piece or goes off
// the board.
//
// p : Current board state.
// c : Color of king shooting laser.
// laser_map : End result will be stored here. Every square on the
//             path of the laser is marked with mark_mask.
// mark_mask : What each square is marked with.
void mark_laser_path(position_t* p, color_t c, char* laser_map,
                     char mark_mask) {
  square_t sq = p->kloc[c];
  int bdir = ori_of(p->board[sq]);

  tbassert(ptype_of(p->board[sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[sq]));
  laser_map[sq] |= mark_mask;

  while (true) {
    sq += beam_of(bdir);
    laser_map[sq] |= mark_mask;
    tbassert(sq < ARR_SIZE && sq >= 0, "sq: %d\n", sq);

    switch (ptype_of(p->board[sq])) {
    case EMPTY:  // empty square
      break;
    case PAWN:  // Pawn
      bdir = reflect_of(bdir, ori_of(p->board[sq]));
      if (bdir < 0) {  // Hit back of Pawn
        return;
      }
      break;
    case KING:  // King
      return;  // sorry, game over my friend!
      break;
    case INVALID:  // Ran off edge of board
      return;
      break;
    default:  // Shouldna happen, man!
      tbassert(false, "Not cool, man.  Not cool.\n");
      break;
    }
  }
}

// Marks the path/line-of-sight of the laser until it hits a piece or goes off
// the board.
// Increment for each time you touch a square with the laser
//
// p : Current board state.
// c : Color of king shooting laser.
// laser_map : End result will be stored here. Every square on the
//             path of the laser is marked with mark_mask.
// mark_mask : What each square is marked with.
void add_laser_path(position_t* p, color_t c, float* laser_map) {
  square_t sq = p->kloc[c];
  int bdir = ori_of(p->board[sq]);
  int length = 1;

  tbassert(ptype_of(p->board[sq]) == KING,
           "ptype: %d square: %i\n", ptype_of(p->board[sq]), sq);
  // laser_map[sq] += touch_weight;

  while (true) {
    sq += beam_of(bdir);

    // set laser map to min
    if(laser_map[sq] > length) {
      laser_map[sq] = length;
    }
    length++;
    
    tbassert(sq < ARR_SIZE && sq >= 0, "sq: %d\n", sq);

    switch (ptype_of(p->board[sq])) {
    case EMPTY:  // empty square
      break;
    case PAWN:  // Pawn
      bdir = reflect_of(bdir, ori_of(p->board[sq]));
      if (bdir < 0) {  // Hit back of Pawn
        return;
      }

      // if bouncing off an opposing pawn, add extra to the length of path
      // because the opponent can affect it 
      if (color_of(p->board[sq]) != c) {
        length += 2;
      }
      break;
    case KING:  // King
      return;  // sorry, game over my friend!
      break;
    case INVALID:  // Ran off edge of board
      return;
      break;
    default:  // Shouldna happen, man!
      tbassert(false, "Not cool, man.  Not cool.\n");
      break;
    }
  }
}

float mult_dist_lookup_table[10][10] = {
  {1.0, 0.5, 0.333333, 0.25, 0.2, 0.166667, 0.142857, 0.125, 0.111111, 0.1},
  {0.5, 0.25, 0.166667, 0.125, 0.1, 0.083333, 0.071429, 0.0625, 0.055556, 0.05},
  {0.333333, 0.166667, 0.111111, 0.083333, 0.066667, 0.055556, 0.047619, 0.041667, 0.037037, 0.03333},
  {0.25, 0.125, 0.083333, 0.0625, 0.05, 0.041667, 0.035714, 0.03125, 0.027778, 0.025},
  {0.2, 0.1, 0.066667, 0.05, 0.04, 0.033333, 0.028571, 0.025, 0.022222, 0.02},
  {0.166667, 0.083333, 0.055556, 0.041667, 0.033333, 0.027778, 0.02381, 0.020833, 0.018519, 0.016667},
  {0.142857, 0.071429, 0.047619, 0.035714, 0.028571, 0.02381, 0.020408, 0.017857, 0.015873, 0.014286},
  {0.125, 0.0625, 0.041667, 0.03125, 0.025, 0.020833, 0.017857, 0.015625, 0.013889, 0.0125},
  {0.111111, 0.055556, 0.037037, 0.027778, 0.022222, 0.018519, 0.015873, 0.013889, 0.012346, 0.011111},
  {0.1, 0.05, 0.033333, 0.025, 0.02, 0.016667, 0.014286, 0.0125, 0.011111, 0.01}
};

float mult_dist(square_t a, square_t b) {
  int8_t delta_fil = abs(fil_of(a) - fil_of(b));
  int8_t delta_rnk = abs(rnk_of(a) - rnk_of(b));
  if (delta_fil == 0 && delta_rnk == 0) {
    return 2;
  }

  return mult_dist_lookup_table[delta_rnk][delta_fil];
}

// Manhattan distance
int manhattan_dist(square_t a, square_t b) {
  int delta_fil = abs(fil_of(a) - fil_of(b));
  int delta_rnk = abs(rnk_of(a) - rnk_of(b));
  return delta_fil + delta_rnk;
}

float laser_coverage_ref(position_t* p, color_t color) {
  position_t np;
  sortable_move_t moves[MAX_NUM_MOVES];
  int num_moves = generate_all_with_color(p, moves, color);
  int i;

  float coverage_map[ARR_SIZE];

  // initialization
  for (int i = 0; i < ARR_SIZE; ++i) {
    coverage_map[i] = FLT_MAX;
  }

  // increment laser path for each possible move
  for(i = 0; i < num_moves; i++) {
    move_t mv = get_move(moves[i]);

    low_level_make_move(p, &np, mv); // make the move

    add_laser_path(&np, color, coverage_map);  // increment laser path
  }

  // get square of opposing king
  square_t king_sq = p->kloc[color];
  // get square of opposing king
  square_t opp_king_sq = p->kloc[opp_color(color)];

  // name it something besides laser_map
  // initialize laser map
  float result = 0;

  // add in everything on board
  for (fil_t f = 0; f < BOARD_WIDTH; ++f) {
    for (rnk_t r = 0; r < BOARD_WIDTH; ++r) {
      if (coverage_map[square_of(f, r)] < FLT_MAX) {
        // length of path divided by length of shortest possible path
        //tbassert(manhattan_dist(king_sq, square_of(f, r)) <= coverage_map[square_of(f, r)], "f: %d, r: %d, dist = %d, map: %f\n", f, r, manhattan_dist(king_sq, square_of(f, r)), coverage_map[square_of(f, r)]);

        // printf("before: %f, dist: %d\n", coverage_map[square_of(f, r)], manhattan_dist(king_sq, square_of(f, r)));
        coverage_map[square_of(f, r)] = ((manhattan_dist(king_sq, square_of(f, r))) / (coverage_map[square_of(f, r)]));

        tbassert(mult_dist(square_of(f, r), opp_king_sq) > 0, "mult_distance must be positive");
        coverage_map[square_of(f, r)] *= mult_dist(square_of(f, r), opp_king_sq);
        result += coverage_map[square_of(f, r)];
      }
    }
  }

  // add in off-board weights
  // add in top row
  rnk_t r;
  fil_t f;
  r = -1;
  for(f = -1; f < BOARD_WIDTH + 1; f++) {
    result += mult_dist(square_of(f, r), opp_king_sq);
  }
  // add in bottom row
  r = BOARD_WIDTH;
  for(f = -1; f < BOARD_WIDTH + 1; f++) {
    result += mult_dist(square_of(f, r), opp_king_sq);
  }

  // add in left col (minus top and bottom)
  f = -1;
  for(r = 0; r < BOARD_WIDTH; r++) {
    result += mult_dist(square_of(f, r), opp_king_sq);
  }

  // add in right col (minus top and bottom)
  f = BOARD_WIDTH;
  for(r = 0; r < BOARD_WIDTH; r++) {
    result += mult_dist(square_of(f, r), opp_king_sq);
  }

  return result;
}

float laser_coverage(position_t* p, color_t color) {
  position_t np;
  sortable_move_t moves[MAX_NUM_MOVES];
  int num_moves = generate_all_with_color(p, moves, color);

  // get square of opposing king
  square_t king_sq = p->kloc[color];
  // get square of opposing king
  square_t opp_king_sq = p->kloc[opp_color(color)];

  float coverage_map[ARR_SIZE];
  char laser_path[ARR_SIZE];

  // initialization
  for (int i = 0; i < ARR_SIZE; ++i) {
    coverage_map[i] = FLT_MAX;
    laser_path[i] = 0;
  }

  mark_laser_path(p, color, laser_path, 1);
  add_laser_path(p, color, coverage_map);

  square_t f_square;
  square_t t_square;
  square_t i_square;
  // increment laser path for each possible move
  for (int i = 0; i < num_moves; i++) {
    move_t mv = get_move(moves[i]);

    f_square = from_square(mv);
    t_square = to_square(mv);
    i_square = intermediate_square(mv);

    if (laser_path[f_square] || laser_path[t_square] || laser_path[i_square]) {
      low_level_make_move(p, &np, mv); // make the move
      add_laser_path(&np, color, coverage_map);  // increment laser path
    }
  }

  //memcpy(in_coverage_map, coverage_map, sizeof coverage_map);

  // name it something besides laser_map
  // initialize laser map
  float result = 0;
  square_t square;
  float coverage_map_val;
  // add in everything on board
  for (fil_t f = 0; f < BOARD_WIDTH; ++f) {
    for (rnk_t r = 0; r < BOARD_WIDTH; ++r) {
      square = square_of(f,r);
      coverage_map_val = coverage_map[square];
      if (coverage_map_val < FLT_MAX) {
        // length of path divided by length of shortest possible path
        // tbassert(manhattan_dist(king_sq, square_of(f, r)) <= coverage_map[square_of(f, r)], "f: %d, r: %d, dist = %d, map: %f\n", f, r, manhattan_dist(king_sq, square_of(f, r)), coverage_map[square_of(f, r)]);

        // printf("before: %f, dist: %d\n", coverage_map[square_of(f, r)], manhattan_dist(king_sq, square_of(f, r)));
        coverage_map_val = ((manhattan_dist(king_sq, square)) / coverage_map_val);

        tbassert(mult_dist(square, opp_king_sq) > 0, "mult_distance must be positive");
        coverage_map_val *= mult_dist(square, opp_king_sq);
        result += coverage_map_val;
        coverage_map[square] = coverage_map_val;
      }
    }
  }

  // add in off-board weights
  rnk_t r;
  fil_t f;
  for (f = -1; f < BOARD_WIDTH + 1; f++) {
    // add in top row
    result += mult_dist(square_of(f, -1), opp_king_sq);
    // add in bottom row
    result += mult_dist(square_of(f, BOARD_WIDTH), opp_king_sq);
  }

  for (r = 0; r < BOARD_WIDTH; r++) {
    // add in lef col
    result += mult_dist(square_of(-1, r), opp_king_sq);
    // add in right col
    result += mult_dist(square_of(BOARD_WIDTH, r), opp_king_sq);
  }

  tbassert(fabsf(result - laser_coverage_ref(p, color)) < .01, "YIANNI IS WRONG. his version: %f ref version: %f\n", result, laser_coverage_ref(p, color));

  return result;
}

// MOBILITY heuristic: safe squares around king of given color.
int mobility(position_t* p, color_t color) {
  color_t c = opp_color(color);
  char laser_map[ARR_SIZE];
  memcpy(laser_map, blank_laser_map, sizeof(laser_map));

  mark_laser_path(p, c, laser_map, 1);  // find path of laser given that you aren't moving

  int mobility = 0;
  square_t king_sq = p->kloc[color];
  tbassert(ptype_of(p->board[king_sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[king_sq]));
  tbassert(color_of(p->board[king_sq]) == color,
           "color: %d\n", color_of(p->board[king_sq]));

  if (laser_map[king_sq] == 0) {
    mobility++;
  }
  for (int d = 0; d < 8; ++d) {
    square_t sq = king_sq + dir_of(d);
    if (laser_map[sq] == 0) {
      mobility++;
    }
  }
  return mobility;
}

// Static evaluation.  Returns score
score_t eval(position_t* p, bool verbose) {
  // seed rand_r with a value of 1, as per
  // http://linux.die.net/man/3/rand_r
  static __thread unsigned int seed = 1;
  // verbose = true: print out components of score
  ev_score_t score[2] = { 0, 0 };
  //  int corner[2][2] = { {INF, INF}, {INF, INF} };
  ev_score_t bonus;
  char buf[MAX_CHARS_IN_MOVE];

  square_t king_square;
  // fil_t f;
  // rnk_t r;
  //----------------------------------------------
  // handle WHITE king
  //----------------------------------------------
  king_square = p->pieceLocations[WHITE][0];
  rnk_t white_rnk = rnk_of(king_square);
  fil_t white_fil = fil_of(king_square);
  if (king_square != -1){
    score[WHITE] = kface(p, white_fil, white_rnk) + kaggressive(p, white_fil, white_rnk);
  }

  //----------------------------------------------
  // handle BLACK king
  //----------------------------------------------
  king_square = p->pieceLocations[BLACK][0];
  rnk_t black_rnk = rnk_of(king_square);
  rnk_t black_fil = fil_of(king_square);
  if (king_square != -1){
    score[BLACK] = kface(p, black_fil, black_rnk) + kaggressive(p, black_fil, black_rnk);
  }

  for (int cInd = 0; cInd < 2; cInd++){
    color_t c = cInd;
    square_t* pieces_of_color = p->pieceLocations[cInd];
    for (int i = 1; i < NUM_PIECES_SIDE; i++) {
      square_t sq = pieces_of_color[i];
      if (sq != -1) {
        fil_t f = fil_of(sq);
        rnk_t r = rnk_of(sq);
        piece_t x = p->board[sq];
        if (verbose) {
          square_to_str(sq, buf, MAX_CHARS_IN_MOVE);
        }

        switch (ptype_of(x)) {
        case EMPTY:
          break;
        case PAWN:
          // MATERIAL heuristic: Bonus for each Pawn
          // bonus = PAWN_EV_VALUE;
          // if (verbose) {
          //   printf("MATERIAL bonus %d for %s Pawn on %s\n", bonus, color_to_str(c), buf);
          // }
          // score[c] += bonus;

          // // PBETWEEN heuristic
          // bonus = pbetween(f, r, white_fil, white_rnk, black_fil, black_rnk);
          // if (verbose) {
          //   printf("PBETWEEN bonus %d for %s Pawn on %s\n", bonus, color_to_str(c), buf);
          // }
          // score[c] += bonus;

          // // PCENTRAL heuristic
          // bonus = pcentral(f, r);
          // if (verbose) {
          //   printf("PCENTRAL bonus %d for %s Pawn on %s\n", bonus, color_to_str(c), buf);
          // }
          score[c] += PAWN_EV_VALUE + pbetween(f, r, white_fil, white_rnk, black_fil, black_rnk) + pcentral(f, r);
          break;

        case KING:
          printf("Should never get here. eval.c:eval piece is king");
          break;
        case INVALID:
          break;
        default:
          tbassert(false, "Jose says: no way!\n");   // No way, Jose!
        }
      }
    }
  }

 // LASER_COVERAGE heuristic
 float w_coverage = LCOVERAGE * laser_coverage(p, WHITE);
 score[WHITE] += (int) w_coverage;
 if (verbose) {
   printf("COVERAGE bonus %d for White\n",(int) w_coverage);
 }
 float b_coverage = LCOVERAGE * laser_coverage(p, BLACK);
 score[BLACK] += (int) b_coverage;
 if (verbose) {
   printf("COVERAGE bonus %d for Black\n",(int) b_coverage);
 }

//  // MOBILITY heuristic
//  float w_mobility = MOBILITY * mobility(p, WHITE);
//  score[WHITE] += (int) w_mobility;
//  if (verbose) {
//    printf("MOBILITY bonus %d for White\n", (int) w_mobility);
//  }
//  float b_mobility = MOBILITY * mobility(p, BLACK);
//  score[BLACK] += (int) b_mobility;
//  if (verbose) {
//    printf("MOBILITY bonus %d for Black\n", (int) b_mobility);
//  }

 // score from WHITE point of view
 ev_score_t tot = score[WHITE] - score[BLACK];

 if (RANDOMIZE) {
   ev_score_t  z = rand_r(&seed) % (RANDOMIZE * 2 + 1);
   tot = tot + z - RANDOMIZE;
 }

 if (color_to_move_of(p) == BLACK) {
   tot = -tot;
 }

 return tot / EV_SCORE_RATIO;
}
