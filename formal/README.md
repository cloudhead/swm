# Agda model of swm's policy core

Every module is dependency-free and checked in Agda's safe mode.  The proofs
cover these executable models and preconditions:

- `Core.agda` follows every accepted branch of `swm_stack_configure`.  Starting
  with `1 <= msize <= 31` and `stacks >= 1`, commands preserve those bounds;
  flipping is involutive; reset establishes `(16, 1, 1)`; and the workspace
  flag laws follow from the same Boolean definition as `swm_workspace_state`.
- `WorkspaceSwitch.agda` requires the old and target workspaces to differ and
  requires the affected monitor placements to be coherent.  Both switch
  branches select the target, move or hide the old workspace, and preserve the
  fact that two affected workspaces cannot occupy the same monitor.
- `StackLayout.agda` computes the master/stack client split, computes the
  effective stack count, constructs positive row groups covering exactly all
  stack clients, and derives a safe-division witness for every group.  The
  structural traversal uses those coverage equalities to return exactly one
  box per input client.  Width partitions are constructed with an exact sum,
  rather than having their conclusion supplied to a theorem.
- `WorkspaceNavigation.agda` proves that a successful result is the *first*
  eligible candidate: every preceding candidate was rejected.  `nothing`
  proves that every candidate was rejected.
- `Geometry.agda` executes the minimum-size clamp and the two position clamps
  in `swm_box_apply_bounds` order.  With positive output dimensions it proves
  minimum size and strict horizontal and vertical overlap with that particular
  output.  Its resize model includes signed-direction deltas and selected
  edges; it proves the C position equations and preservation of the opposite
  edge whenever the resulting natural coordinate does not underflow.
- `LayoutCycle.agda` proves that a successful result is the first enabled
  layout in the supplied wraparound order, including rejection evidence for
  every preceding layout.  `nothing` proves that no layout in the sequence is
  enabled.

Check everything with:

```sh
make formal
```

The target passes `--safe --no-libraries --ignore-interfaces`; postulates,
unsafe pragmas, stale interfaces, or undeclared library dependencies therefore
cannot make the check succeed.

## Proof boundary

These are proofs about executable mathematical models of the pure C policy,
not a refinement proof of the compiled program.  `Core`, navigation, layout
cycling, and the branch structure of bounds application follow their C
counterparts directly.  Workspace switching models the two workspaces and two
monitor placements affected by a switch.  Stack layout proves the client-count,
positive-divisor, and partition contracts used by the loop; it does not claim
that its placeholder boxes reproduce the C quotient/remainder coordinates.

Geometry uses natural coordinates and dimensions.  The bounds theorem covers
the C postcondition but not the exact negative coordinate chosen when a box is
wider than an output.  The fixed-edge resize theorem states the explicit
non-underflow condition needed to represent the C result as a natural number.

The C correspondence assumes integer arithmetic does not overflow, output
dimensions are positive, stack state entered the layout with `stacks >= 1`, and
pointer, enum, and index validation succeeded.  Allocation, pointer validity,
wlroots, protocols, rendering, and the event loop remain outside the model.
Closing that final model-to-C gap requires a refinement layer, verified
extraction, or generated C; these modules deliberately do not claim one.
