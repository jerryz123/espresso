/*
    module: expand.c
    purpose: Perform the Espresso-II Expansion Step

    The idea is to take each nonprime cube of the on-set and expand it
    into a prime implicant such that we can cover as many other cubes
    of the on-set.  If no cube of the on-set can be covered, then we
    expand each cube into a large prime implicant by transforming the
    problem into a minimum covering problem which is solved by the
    heuristics of minimum_cover.

    These routines revolve around having a representation of the
    OFF-set. (contrast to the Espresso-II manuscript, we do NOT
    require an "unwrapped" version of the OFF-set).

    Some conventions on variable names:

        SUPER_CUBE is the supercube of all cubes which can be covered
        by an expansion of the cube being expanded

        OVEREXPANDED_CUBE is the cube which would result from expanding
        all parts which can expand individually of the cube being expanded

        RAISE is the current expansion of the current cube

        FREESET is the set of parts which haven't been raised or lowered yet.

        INIT_LOWER is a set of parts to be removed from the free parts before
        starting the expansion
*/

#include "espresso.h"

/*
    expand -- expand each nonprime cube of F into a prime implicant

    If nonsparse is true, only the non-sparse variables will be expanded;
    this is done by forcing all of the sparse variables out of the free set.
*/

pcover expand(pcover F, pcover R,
              bool nonsparse /* expand non-sparse variables only */
) {
    pcube last, p;
    pcube INIT_LOWER;
    bool change;

    /* Order the cubes according to "chewing-away from the edges" of mini */
    F = mini_sort(F, ascend);

    /* Allocate memory for variables needed by expand1() */
    INIT_LOWER = new_cube();

    /* Setup the initial lowering set (differs only for nonsparse) */
    if (nonsparse)
        (void)set_or(
            INIT_LOWER, INIT_LOWER,
            cube.var_mask[cube.output]);  // the output variable is "sparse"

    /* Mark all cubes as not covered, and maybe essential */
    foreach_set(F, last, p) {
        RESET(p, COVERED);
        RESET(p, NONESSEN);
    }

    /* Try to expand each nonprime and noncovered cube */
    foreach_set(F, last, p) {
        /* do not expand if PRIME or if covered by previous expansion */
        if (!TESTP(p, PRIME) && !TESTP(p, COVERED)) {
            /* expand the cube p, result is RAISE */
            expand1(R, F, INIT_LOWER, p);
        }
    }

    /* Delete any cubes of F which became covered during the expansion */
    F->active_count = 0;
    change = FALSE;
    foreach_set(F, last, p) {
        if (TESTP(p, COVERED)) {
            RESET(p, ACTIVE);
            change = TRUE;
        } else {
            SET(p, ACTIVE);
            F->active_count++;
        }
    }
    if (change)
        F = sf_inactive(F);

    free_cube(INIT_LOWER);
    return F;
}

/*
    expand1 -- Expand a single cube against the OFF-set
*/
void expand1(pcover BB,        /* Blocking matrix (OFF-set) */
             pcover CC,        /* Covering matrix (ON-set) */
             pcube INIT_LOWER, /* Parts to initially remove from FREESET */
             pcube c           /* The cube to be expanded */
) {
    pcube FREESET = new_cube();    /* The current parts which are free */
    pcube SUPER_CUBE = new_cube(); /* Supercube of all cubes of CC we cover */
    pcube RAISE = new_cube(); /* The current parts which have been raised */
    pcube OVEREXPANDED_CUBE = new_cube(); /* Overexpanded cube of c */
    int num_covered; /* Number of cubes of CC which are covered */

    int bestindex;

    /* initialize BB and CC */
    SET(c, PRIME); /* don't try to cover ourself */
    pcube p, last;

    /* Create the block and cover set families */
    BB->active_count = BB->count;
    foreach_set(BB, last, p) SET(p, ACTIVE);

    CC->active_count = CC->count;
    // clang-format off
    foreach_set(CC, last, p)
        if (TESTP(p, COVERED) || TESTP(p, PRIME)) {
            CC->active_count--;
            RESET(p, ACTIVE);
        } else {
            SET(p, ACTIVE);
        }
    // clang-format on

    /* initialize count of # cubes covered, and the supercube of them */
    num_covered = 0;
    (void)set_copy(SUPER_CUBE, c);

    /* Initialize the lowering, raising and unassigned sets */
    (void)set_copy(RAISE, c);
    (void)set_diff(FREESET, cube.fullset, RAISE);

    /* If some parts are forced into lowering set, remove them */
    if (!setp_empty(INIT_LOWER)) {
        (void)set_diff(FREESET, FREESET, INIT_LOWER);
        elim_lowering(BB, CC, RAISE, FREESET);
    }

    /* Determine what can be raised, and return the over-expanded cube */
    essen_parts(BB, CC, RAISE, FREESET);
    (void)set_or(OVEREXPANDED_CUBE, RAISE, FREESET);

    /* While there are still cubes which can be covered, cover them ! */
    if (CC->active_count > 0) {
        select_feasible(BB, CC, RAISE, FREESET, SUPER_CUBE, &num_covered);
    }

    /* While there are still cubes covered by the overexpanded cube ... */
    while (CC->active_count > 0) {
        bestindex = most_frequent(CC, FREESET);
        set_insert(RAISE, bestindex);
        set_remove(FREESET, bestindex);
        essen_parts(BB, CC, RAISE, FREESET);
    }

    /* Finally, when all else fails, choose the largest possible prime */
    /* We will loop only if we decide unravelling OFF-set is too expensive */
    while (BB->active_count > 0) {
        mincov(BB, RAISE, FREESET);
    }

    /* Raise any remaining free coordinates */
    (void)set_or(RAISE, RAISE, FREESET);

    (void)set_copy(c, RAISE);
    SET(c, PRIME);
    RESET(c, COVERED); /* not really necessary */

    /* See if we generated an inessential prime */
    if (num_covered == 0 && !setp_equal(c, OVEREXPANDED_CUBE)) {
        SET(c, NONESSEN);
    }

    free_cube(FREESET);
    free_cube(SUPER_CUBE);
    free_cube(RAISE);
    free_cube(OVEREXPANDED_CUBE);
}

/*
    essen_parts -- determine which parts are forced into the lowering
    set to insure that the cube be orthognal to the OFF-set.

    If any cube of the OFF-set is distance 1 from the raising cube,
    then we must lower all parts of the conflicting variable.  (If the
    cube is distance 0, we detect this error here.)

    If there are essentially lowered parts, we can remove from consideration
    any cubes of the OFF-set which are more than distance 1 from the
    overexpanded cube of RAISE.
*/

void essen_parts(pcover BB, pcover CC, pcube RAISE, pcube FREESET) {
    pcube p, r = RAISE;
    pcube lastp, xlower = cube.temp[0];
    int dist;

    (void)set_copy(xlower, cube.emptyset);

    foreach_active_set(BB, lastp, p) {
        if ((dist = cdist01(p, r)) <= 1) {
            if (dist == 0) {
                fatal("ON-set and OFF-set are not orthogonal");
            } else {
                (void)force_lower(xlower, p, r);
                BB->active_count--;
                RESET(p, ACTIVE);
            }
        }
    }

    if (!setp_empty(xlower)) {
        (void)set_diff(FREESET, FREESET, xlower); /* remove from free set */
        elim_lowering(BB, CC, RAISE, FREESET);
    }
}

/*
    essen_raising -- determine which parts may always be added to
    the raising set without restricting further expansions

    General rule: if some part is not blocked by any cube of BB, then
    this part can always be raised.
*/

void essen_raising(pcover BB, pcube RAISE, pcube FREESET) {
    pcube last, p, xraise = cube.temp[0];

    /* Form union of all cubes of BB, and then take complement wrt FREESET */
    (void)set_copy(xraise, cube.emptyset);
    foreach_active_set(BB, last, p) INLINEset_or(xraise, xraise, p);
    (void)set_diff(xraise, FREESET, xraise);

    (void)set_or(RAISE, RAISE, xraise);       /* add to raising set */
    (void)set_diff(FREESET, FREESET, xraise); /* remove from free set */
}

/*
    elim_lowering -- after removing parts from FREESET, we can reduce the
    size of both BB and CC.

    We mark as inactive any cube of BB which does not intersect the
    overexpanded cube (i.e., RAISE + FREESET).  Likewise, we remove
    from CC any cube which is not covered by the overexpanded cube.
*/

void elim_lowering(pcover BB, pcover CC, pcube RAISE, pcube FREESET) {
    pcube p, r = set_or(cube.temp[0], RAISE, FREESET);
    pcube last;

    /*
     *  Remove sets of BB which are orthogonal to future expansions
     */
    foreach_active_set(BB, last, p) {
        if (!cdist0(p, r))
            BB->active_count--, RESET(p, ACTIVE);
    }

    /*
     *  Remove sets of CC which cannot be covered by future expansions
     */
    if (CC != (pcover)NULL) {
        foreach_active_set(CC, last, p) {
            if (!setp_implies(p, r))
                CC->active_count--, RESET(p, ACTIVE);
        }
    }
}

/*
    most_frequent -- When all else fails, select a reasonable part to raise
    The active cubes of CC are the cubes which are covered by the
    overexpanded cube of the original cube (however, we know that none
    of them can actually be covered by a feasible expansion of the
    original cube).  We resort to the MINI strategy of selecting to
    raise the part which will cover the same part in the most cubes of CC.
*/
int most_frequent(pcover CC, pcube FREESET) {
    int i, best_part, best_count, *count;
    pset p, last;

    /* Count occurences of each variable */
    count = ALLOC(int, cube.size);
    for (i = 0; i < cube.size; i++)
        count[i] = 0;
    if (CC != (pcover)NULL)
        foreach_active_set(CC, last, p) set_adjcnt(p, count, 1);

    /* Now find which free part occurs most often */
    best_count = best_part = -1;
    for (i = 0; i < cube.size; i++)
        if (is_in_set(FREESET, i) && count[i] > best_count) {
            best_part = i;
            best_count = count[i];
        }
    FREE(count);

    return best_part;
}

/*
    select_feasible -- Determine if there are cubes which can be covered,
    and if so, raise those parts necessary to cover as many as possible.

    We really don't check to maximize the number that can be covered;
    instead, we check, for each fcc, how many other fcc remain fcc
    after expanding to cover the fcc.  (Essentially one-level lookahead).
*/

void select_feasible(pcover BB, pcover CC, pcube RAISE, pcube FREESET,
                     pcube SUPER_CUBE, int *num_covered) {
    pcube p, last, bestfeas, *feas;
    int i, j;
    pcube *feas_new_lower;
    int bestcount, bestsize, count, size, numfeas, lastfeas;
    pcover new_lower;

    /*  Start out with all cubes covered by the over-expanded cube as
     *  the "possibly" feasibly-covered cubes (pfcc)
     */
    feas = ALLOC(pcube, CC->active_count);
    numfeas = 0;
    foreach_active_set(CC, last, p) feas[numfeas++] = p;

    /* Setup extra cubes to record parts forced low after a covering */
    feas_new_lower = ALLOC(pcube, CC->active_count);
    new_lower = new_cover(numfeas);
    for (i = 0; i < numfeas; i++)
        feas_new_lower[i] = GETSET(new_lower, i);

loop:
    /* Find the essentially raised parts -- this might cover some cubes
       for us, without having to find out if they are fcc or not
    */
    essen_raising(BB, RAISE, FREESET);

    /* Now check all "possibly" feasibly covered cubes to check feasibility */
    lastfeas = numfeas;
    numfeas = 0;
    for (i = 0; i < lastfeas; i++) {
        p = feas[i];

        /* Check active because essen_parts might have removed it */
        if (TESTP(p, ACTIVE)) {
            /*  See if the cube is already covered by RAISE --
             *  this can happen because of essen_raising() or because of
             *  the previous "loop"
             */
            if (setp_implies(p, RAISE)) {
                (*num_covered) += 1;
                (void)set_or(SUPER_CUBE, SUPER_CUBE, p);
                CC->active_count--;
                RESET(p, ACTIVE);
                SET(p, COVERED);
                /* otherwise, test if it is feasibly covered */
            } else if (feasibly_covered(BB, p, RAISE,
                                        feas_new_lower[numfeas])) {
                feas[numfeas] = p; /* save the fcc */
                numfeas++;
            }
        }
    }
    /* Exit here if there are no feasibly covered cubes */
    if (numfeas == 0) {
        FREE(feas);
        FREE(feas_new_lower);
        free_cover(new_lower);
        return;
    }

    /* Now find which is the best feasibly covered cube */
    bestcount = 0;
    bestsize = 9999;
    for (i = 0; i < numfeas; i++) {
        size = set_dist(feas[i], FREESET); /* # of newly raised parts */
        count = 0; /* # of other cubes which remain fcc after raising */

#define NEW
#ifdef NEW
        for (j = 0; j < numfeas; j++)
            if (setp_disjoint(feas_new_lower[i], feas[j]))
                count++;
#else
        for (j = 0; j < numfeas; j++)
            if (setp_implies(feas[j], feas[i]))
                count++;
#endif
        if (count > bestcount) {
            bestcount = count;
            bestfeas = feas[i];
            bestsize = size;
        } else if (count == bestcount && size < bestsize) {
            bestfeas = feas[i];
            bestsize = size;
        }
    }

    /* Add the necessary parts to the raising set */
    (void)set_or(RAISE, RAISE, bestfeas);
    (void)set_diff(FREESET, FREESET, RAISE);
    essen_parts(BB, CC, RAISE, FREESET);
    goto loop;
    /* NOTREACHED */
}

/*
    feasibly_covered -- determine if the cube c is feasibly covered
    (i.e., if it is possible to raise all of the necessary variables
    while still insuring orthogonality with R).  Also, if c is feasibly
    covered, then compute the new set of parts which are forced into
    the lowering set.
*/

bool feasibly_covered(pcover BB, pcube c, pcube RAISE, pcube new_lower) {
    pcube p, r = set_or(cube.temp[0], RAISE, c);
    int dist;
    pcube lastp;

    set_copy(new_lower, cube.emptyset);
    foreach_active_set(BB, lastp, p) {
        if ((dist = cdist01(p, r)) <= 1) {
            if (dist == 0)
                return FALSE;
            else
                (void)force_lower(new_lower, p, r);
        }
    }
    return TRUE;
}

/*
    mincov -- transform the problem of expanding a cube to a maximally-
    large prime implicant into the problem of selecting a minimum
    cardinality cover over a family of sets.

    When we get to this point, we must unravel the remaining off-set.
    This may be painful.
*/

void mincov(pcover BB, pcube RAISE, pcube FREESET) {
    int expansion, nset, dist;
    pset_family B;
    pcube xraise = cube.temp[0], xlower, p, last, plower;

#ifdef RANDOM_MINCOV
    dist = random() % set_ord(FREESET);
    for (var = 0; var < cube.size && dist >= 0; var++) {
        if (is_in_set(FREESET, var)) {
            dist--;
        }
    }

    set_insert(RAISE, var);
    set_remove(FREESET, var);
    (void)essen_parts(BB, /*CC*/ (pcover)NULL, RAISE, FREESET);
#else

    /* Create B which are those cubes which we must avoid intersecting */
    B = new_cover(BB->active_count);
    foreach_active_set(BB, last, p) {
        plower = set_copy(GETSET(B, B->count++), cube.emptyset);
        (void)force_lower(plower, p, RAISE);
    }

    /* Determine how many sets it will blow up into after the unravel */
    nset = 0;
    foreach_set(B, last, p) {
        expansion = 1;
        if ((dist = set_dist(p, cube.var_mask[cube.output])) > 1) {
            expansion *= dist;
            if (expansion > 500)
                goto heuristic_mincov;
        }
        nset += expansion;
        if (nset > 500)
            goto heuristic_mincov;
    }

    B = unravel_output(B);
    xlower = do_sm_minimum_cover(B);

    /* Add any remaining free parts to the raising set */
    (void)set_or(RAISE, RAISE, set_diff(xraise, FREESET, xlower));
    (void)set_copy(FREESET, cube.emptyset); /* free set is empty */
    BB->active_count = 0;                   /* BB satisfied */
    sf_free(B);
    set_free(xlower);
    return;

heuristic_mincov:
    sf_free(B);
    /* most_frequent will pick first free part */
    set_insert(RAISE, most_frequent(/*CC*/ (pcover)NULL, FREESET));
    (void)set_diff(FREESET, FREESET, RAISE);
    essen_parts(BB, /*CC*/ (pcover)NULL, RAISE, FREESET);
    return;
#endif
}
