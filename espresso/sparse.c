/*
    module: sparse.c

    make_sparse is a last-step cleanup to reduce the total number
    of literals in the cover.

    This is done by reducing the "sparse" variables (using a modified
    version of irredundant rather than reduce), followed by expanding
    the "dense" variables (using modified version of expand).
*/

#include "espresso.h"

pcover make_sparse(pcover F, pcover D, pcover R) {
    cost_t cost, best_cost;

    cover_cost(F, &best_cost);

    do {
        F = mv_reduce(F, D);
        if (cost.total == best_cost.total)
            break;
        copy_cost(&cost, &best_cost);

        F = expand(F, R, TRUE);
        if (cost.total == best_cost.total)
            break;
        copy_cost(&cost, &best_cost);
    } while (TRUE);

    return F;
}

/*
    mv_reduce -- perform an "optimal" reduction of the variables which
    we desire to be sparse

    This could be done using "reduce" and then saving just the desired
    part of the reduction.  Instead, this version uses IRRED to find
    which cubes of an output are redundant.  Note that this gets around
    the cube-ordering problem.

    In normal use, it is expected that the cover is irredundant and
    hence no cubes will be reduced to the empty cube (however, this is
    checked for and such cubes will be deleted)
*/

pcover mv_reduce(pcover F, pcover D) {
    int i;
    pcube p, p1, last;
    int index;
    pcover F1, D1;
    pcube *F_cube_table;

    /* loop for each part of the variable */
    for (i = cube.first_part[cube.output]; i <= cube.last_part[cube.output];
         i++) {
        /* remember mapping of F1 cubes back to F cubes */
        F_cube_table = ALLOC(pcube, F->count);

        /* 'cofactor' against part #i of variable #var */
        F1 = new_cover(F->count);
        foreach_set(F, last, p) {
            if (is_in_set(p, i)) {
                F_cube_table[F1->count] = p;
                p1 = GETSET(F1, F1->count++);
                (void)set_diff(p1, p, cube.var_mask[cube.output]);
                set_insert(p1, i);
            }
        }

        /* 'cofactor' against part #i of variable #var */
        /* not really necessary -- just more efficient ? */
        D1 = new_cover(D->count);
        foreach_set(D, last, p) {
            if (is_in_set(p, i)) {
                p1 = GETSET(D1, D1->count++);
                (void)set_diff(p1, p, cube.var_mask[cube.output]);
                set_insert(p1, i);
            }
        }

        mark_irredundant(F1, D1);

        /* now remove part i from cubes which are redundant */
        index = 0;
        foreach_set(F1, last, p1) {
            if (!TESTP(p1, ACTIVE)) {
                p = F_cube_table[index];

                // reduce the output variable
                set_remove(p, i);
                RESET(p, PRIME);
            }
            index++;
        }

        free_cover(F1);
        free_cover(D1);
        FREE(F_cube_table);
    }

    /* Check if any cubes disappeared */
    (void)sf_active(F);
    foreach_active_set(F, last, p) {
        if (setp_disjoint(p, cube.var_mask[cube.output])) {
            RESET(p, ACTIVE);
            F->active_count--;
        }
    }

    if (F->count != F->active_count) {
        F = sf_inactive(F);
    }
    return F;
}
