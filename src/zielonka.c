#include <stdio.h>
#include <stdlib.h>

#include "pg_attractor.h"
#include "zielonka.h"

enum { MAX_RECURSION_DEPTH = 1024 };

static void solver_error(ZielonkaError *error, char const *message) {
  if (error != nullptr && error->message[0] == '\0') {
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
  }
}

[[nodiscard]] static bool initialize_result(ZielonkaResult *result,
                                            size_t const vertex_count) {
  *result = (ZielonkaResult){0};
  return pg_set_init(&result->winning[PG_EVEN], vertex_count) &&
         pg_set_init(&result->winning[PG_ODD], vertex_count);
}

[[nodiscard]] static bool domain_within_bound(PGGame const *game,
                                              PGSet const *domain,
                                              uint64_t const bound) {
  for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(domain, vertex + 1)) {
    if (game->vertices[vertex].priority > bound) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] static bool solve(PGGame const *game, PGSet const *domain,
                                uint64_t const bound, size_t const depth,
                                ZielonkaResult *result, ZielonkaError *error) {
  if (depth > MAX_RECURSION_DEPTH) {
    solver_error(error, "priority depth exceeds the safe recursion depth");
    return false;
  }
  if (!initialize_result(result, game->vertex_count)) {
    solver_error(error, "failed to allocate winning-region sets");
    zielonka_result_destroy(result);
    return false;
  }
  if (pg_set_empty(domain)) {
    return true;
  }
  if (!domain_within_bound(game, domain, bound) ||
      !pg_subgame_is_total(game, domain)) {
    solver_error(error, "solver domain violates its bound or totality");
    goto failure;
  }

  if (bound == 0) {
    ADNode *node = ad_node_create(PG_EVEN, 0, game->vertex_count);
    if (node == nullptr || !pg_set_clone(&node->top_attractor, domain) ||
        !pg_set_clone(&result->winning[PG_EVEN], domain)) {
      ad_node_destroy(node);
      solver_error(error, "failed to construct the bound-zero witness");
      goto failure;
    }
    result->decomposition[PG_EVEN] = node;
    return true;
  }

  PGPlayer const alpha = (PGPlayer)(bound % 2);
  PGPlayer const beta = (PGPlayer)(1 - alpha);
  PGSet residual = {0};
  if (!pg_set_clone(&residual, domain)) {
    solver_error(error, "failed to allocate the residual domain");
    goto failure;
  }
  ADNode *beta_accumulator =
      ad_node_create(beta, bound + 1, game->vertex_count);
  if (beta_accumulator == nullptr) {
    solver_error(error, "failed to allocate the decomposition accumulator");
    pg_set_destroy(&residual);
    goto failure;
  }

  while (!pg_set_empty(&residual)) {
    PGSet target = {0};
    PGSet alpha_attractor = {0};
    PGSet lower = {0};
    ZielonkaResult recursive = {0};
    if (!pg_set_init(&target, game->vertex_count) ||
        !pg_set_clone(&lower, &residual)) {
      solver_error(error, "failed to allocate an iteration set");
      goto iteration_failure;
    }
    for (size_t vertex = pg_set_next(&residual, 0); vertex != SIZE_MAX;
         vertex = pg_set_next(&residual, vertex + 1)) {
      if (game->vertices[vertex].priority == bound) {
        pg_set_add(&target, vertex);
      }
    }
    if (!pg_attractor(game, &residual, &target, alpha, &alpha_attractor)) {
      solver_error(error, "failed to compute the top attractor");
      goto iteration_failure;
    }
    pg_set_subtract_into(&lower, &alpha_attractor);
    if (!solve(game, &lower, bound - 1, depth + 1, &recursive, error)) {
      goto iteration_failure;
    }

    if (pg_set_empty(&recursive.winning[beta])) {
      ADNode *alpha_witness = recursive.decomposition[alpha];
      recursive.decomposition[alpha] = nullptr;
      if (alpha_witness == nullptr) {
        alpha_witness = ad_node_create(alpha, bound, game->vertex_count);
      }
      if (alpha_witness == nullptr ||
          !pg_set_empty(&alpha_witness->top_attractor)) {
        ad_node_destroy(alpha_witness);
        solver_error(error, "invalid terminating alpha witness");
        goto iteration_failure;
      }
      pg_set_move(&alpha_witness->top_attractor, &alpha_attractor);

      PGSet beta_region = {0};
      if (!pg_set_clone(&beta_region, domain)) {
        ad_node_destroy(alpha_witness);
        solver_error(error, "failed to assemble the final winning regions");
        goto iteration_failure;
      }
      pg_set_subtract_into(&beta_region, &residual);
      pg_set_move(&result->winning[alpha], &residual);
      pg_set_move(&result->winning[beta], &beta_region);
      result->decomposition[alpha] = alpha_witness;
      if (beta_accumulator->child_count == 0) {
        ad_node_destroy(beta_accumulator);
      } else {
        result->decomposition[beta] = beta_accumulator;
      }
      pg_set_destroy(&target);
      pg_set_destroy(&alpha_attractor);
      pg_set_destroy(&lower);
      zielonka_result_destroy(&recursive);
      return true;
    }

    PGSet beta_attractor = {0};
    if (recursive.decomposition[beta] == nullptr ||
        !pg_attractor(game, &residual, &recursive.winning[beta], beta,
                      &beta_attractor) ||
        pg_set_empty(&beta_attractor) ||
        !pg_set_subset(&recursive.winning[beta], &beta_attractor) ||
        !pg_set_subset(&beta_attractor, &residual)) {
      solver_error(error, "invalid nonterminal opponent dominion");
      pg_set_destroy(&beta_attractor);
      goto iteration_failure;
    }
    ADChild child = {.subtree = recursive.decomposition[beta]};
    recursive.decomposition[beta] = nullptr;
    pg_set_move(&child.trap, &recursive.winning[beta]);
    pg_set_move(&child.attractor, &beta_attractor);
    if (child.subtree->player != beta ||
        child.subtree->priority_bound != bound - 1 ||
        !ad_node_append_child(beta_accumulator, &child)) {
      pg_set_destroy(&child.trap);
      pg_set_destroy(&child.attractor);
      ad_node_destroy(child.subtree);
      solver_error(error, "failed to append an opponent dominion");
      goto iteration_failure;
    }
    size_t const previous_size = pg_set_count(&residual);
    pg_set_subtract_into(
        &residual,
        &beta_accumulator->children[beta_accumulator->child_count - 1]
             .attractor);
    if (pg_set_count(&residual) >= previous_size) {
      solver_error(error, "opponent attractor did not shrink the residual");
      goto iteration_failure;
    }

    pg_set_destroy(&target);
    pg_set_destroy(&alpha_attractor);
    pg_set_destroy(&lower);
    zielonka_result_destroy(&recursive);
    continue;

  iteration_failure:
    pg_set_destroy(&target);
    pg_set_destroy(&alpha_attractor);
    pg_set_destroy(&lower);
    zielonka_result_destroy(&recursive);
    ad_node_destroy(beta_accumulator);
    pg_set_destroy(&residual);
    goto failure;
  }

  if (!pg_set_clone(&result->winning[beta], domain)) {
    solver_error(error, "failed to store the final opponent winning region");
    ad_node_destroy(beta_accumulator);
    pg_set_destroy(&residual);
    goto failure;
  }
  result->decomposition[beta] = beta_accumulator;
  pg_set_destroy(&residual);
  return true;

failure:
  zielonka_result_destroy(result);
  return false;
}

bool zielonka_decompose(PGGame const *game, PGSet const *domain,
                        uint64_t const bound, ZielonkaResult *result,
                        ZielonkaError *error) {
  if (error != nullptr) {
    *error = (ZielonkaError){0};
  }
  if (game == nullptr || domain == nullptr || result == nullptr ||
      domain->bit_count != game->vertex_count || bound == UINT64_MAX) {
    solver_error(error, "invalid solver input or maximum priority bound");
    return false;
  }
  if (bound > MAX_RECURSION_DEPTH) {
    solver_error(error, "priority depth exceeds the safe recursion depth");
    return false;
  }
  *result = (ZielonkaResult){0};
  return solve(game, domain, bound, 0, result, error);
}
