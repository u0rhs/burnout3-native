/* tune_abi.h -- the tuning console between native_play and the renderer (tune4; docs/modding/tuning.md). native_play
 * records the registry (tools/native/tuning_console.h) and hands the renderer these functions once, through
 * b3r_set_tune_hooks; the renderer's DEVELOPER tab and F9 overlay read the variables through them and ask for edits.
 * Plain C: the two sides are separate libraries. */
#ifndef B3TUNE_TUNE_ABI_H
#define B3TUNE_TUNE_ABI_H

#include <stdint.h>

#define B3TUNE_MAX_WORDS 16

/* One variable as the game thread last saw it (a copy: the strings stay valid for the whole run) */
typedef struct B3TuneVar {
    uint32_t index;          /* registration order */
    int32_t key;             /* CGtHash(name + group + "/" + file) */
    uint32_t address;        /* the guest variable */
    uint32_t type;           /* 1 int, 2 float, 5 vector */
    uint32_t count;          /* the registration's count (>= 2: an array) */
    uint32_t elements;       /* editable elements (a vector: x y z) */
    uint32_t flags;          /* bit 0: a range */
    uint32_t min_word, max_word;
    uint32_t live;           /* registered now (its value is the game's) */
    uint32_t registrations;  /* times registered */
    uint32_t edited;         /* bit e: element e was edited this session */
    uint32_t words[B3TUNE_MAX_WORDS];   /* the current values (raw), when live */
    const char* group;
    const char* name;
    const char* file;
} B3TuneVar;

/* tunenav1: the player's race car as the game thread last saw it (the console's YOUR CAR page; b3tune::car in
   b3tune/tune.h has the addresses). active 0: no race now (the other values are 0). */
typedef struct B3TuneCar {
    uint32_t size;           /* sizeof(B3TuneCar) */
    uint32_t active;         /* a race is on and the player's car is set */
    uint32_t car;            /* its guest address */
    float speed;             /* m/s, the speedometer's source */
    float speed_factor;      /* the speedometer's unit factor (2.23694: mph) */
    float boost, boost_max, boost_rate;
    uint32_t boosting;       /* the game's: boosting now */
    uint32_t infinite_flag;  /* the game's infinite boost flag */
    float health;            /* 0 .. 1 */
    uint32_t freeze;         /* the console's FREEZE DAMAGE */
    uint32_t infinite;       /* the console's INFINITE BOOST */
    /* tunenav2 (CHANGED, reset): bit f set while car field f (b3tune::car::Field) differs from the game's own: FREEZE
       DAMAGE or INFINITE BOOST on, the boost use rate as the console set it (until the game sets it again) */
    uint32_t changed;
    float boost_rate_default;  /* the boost use rate the game had before the console's change (bit kBoostRate) */
} B3TuneCar;

/* tunenav2: one element that differs from its default now because of this session's edits (the default: the value
   the game gave it, after the active mods' presets; B3TuneHooks::changes) */
typedef struct B3TuneChange {
    uint32_t index;          /* the variable (B3TuneHooks::get's i) */
    uint32_t element;
    uint32_t word;           /* its value now (not live: the edit kept for when it registers again) */
    uint32_t def;            /* its default (raw) */
    uint32_t live;           /* registered now */
} B3TuneChange;

typedef struct B3TuneHooks {
    uint32_t size;           /* sizeof(B3TuneHooks): newer members go at the end */
    /* the variables recorded so far (an index below it stays valid) */
    int (*count)(void);
    /* variable i into *out; 0: no such variable */
    int (*get)(int i, B3TuneVar* out);
    /* an edit: element `element` of variable i (and every live variable with its key) to `word`, at the next VBlank;
       clamped to its range */
    void (*edit)(int i, int element, uint32_t word);
    /* the session's edits written as a new mod (a preset): its folder in `msg`; 0: not written (msg says why) */
    int (*save_preset)(char* msg, int n);
    /* one line for the page: registrations, variables, edits, the presets in use */
    int (*status)(char* text, int n);
    /* tunenav1: changes whenever a variable is recorded or one becomes live / not live (the browser's tree is built
       again only then) */
    uint32_t (*generation)(void);
    /* tunenav1: the player's car into *out (out->size set by the caller); 0: no console */
    int (*car)(B3TuneCar* out);
    /* tunenav1: an edit of the player's car (b3tune::car::Field), written and recorded at the next VBlank */
    void (*car_edit)(int field, uint32_t word);
    /* tunenav2: every changed element now (live or kept for a later registration), in registration order: up to n into
       out; returns how many there are (may be more than n) */
    int (*changes)(B3TuneChange* out, int n);
    /* tunenav2: back to the default at the next VBlank, recorded as an edit with the default's word: element `element`
       of variable i (element -1: each of its elements); i -1: every changed value and the car page's FREEZE DAMAGE,
       INFINITE BOOST and boost use rate. Only what differs from its default then is written. */
    void (*reset)(int i, int element);
} B3TuneHooks;

#endif
