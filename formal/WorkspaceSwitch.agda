{-# OPTIONS --safe #-}

module formal.WorkspaceSwitch where

open import Agda.Builtin.Equality

infix 4 _≠_

data ⊥ : Set where

_≠_ : {A : Set} → A → A → Set
x ≠ y = x ≡ y → ⊥

≠-symmetric : {A : Set} {x y : A} → x ≠ y → y ≠ x
≠-symmetric different refl = different refl

-- A workspace is either hidden or points at one monitor.
data Placement (Monitor : Set) : Set where
  hidden : Placement Monitor
  shown  : Monitor → Placement Monitor

-- Two distinct workspaces are coherent when they are not shown by the same
-- monitor.  This is the part of the global workspace/monitor bijection touched
-- by view_workspace.
data Compatible {Monitor : Set} : Placement Monitor → Placement Monitor → Set where
  left-hidden  : {p : Placement Monitor} → Compatible hidden p
  right-hidden : {p : Placement Monitor} → Compatible p hidden
  separate     : {a b : Monitor} → a ≠ b → Compatible (shown a) (shown b)

record SwitchInput (Workspace Monitor : Set) : Set where
  constructor before
  field
    old target : Workspace
    selected : Monitor
    targetWas : Placement Monitor
    different-workspaces : old ≠ target
    coherent-before : Compatible (shown selected) targetWas
open SwitchInput public

record SwitchOutput {Workspace Monitor : Set} (i : SwitchInput Workspace Monitor) : Set where
  constructor after
  field
    selectedNowShows : Workspace
    targetNowOwnedBy : Placement Monitor
    oldNowOwnedBy : Placement Monitor
    selected-is-target : selectedNowShows ≡ target i
    target-is-selected : targetNowOwnedBy ≡ shown (selected i)
    coherent-after : Compatible targetNowOwnedBy oldNowOwnedBy
open SwitchOutput public

-- Model of view_workspace after its null and same-workspace guards.  The
-- coherence proof rules out the impossible case where target is already shown
-- by selected.  If target is shown elsewhere, old moves there; otherwise old
-- becomes hidden.
switch : {W M : Set} (i : SwitchInput W M) → SwitchOutput i
switch (before old target selected hidden distinct right-hidden) =
  after target (shown selected) hidden refl refl right-hidden
switch (before old target selected (shown other) distinct (separate monitors-differ)) =
  after target (shown selected) (shown other) refl refl (separate monitors-differ)

target-selected : {W M : Set} (i : SwitchInput W M) →
  selectedNowShows (switch i) ≡ target i
target-selected i = selected-is-target (switch i)

target-owned-by-selected : {W M : Set} (i : SwitchInput W M) →
  targetNowOwnedBy (switch i) ≡ shown (selected i)
target-owned-by-selected i = target-is-selected (switch i)

switch-preserves-coherence : {W M : Set} (i : SwitchInput W M) →
  Compatible (targetNowOwnedBy (switch i)) (oldNowOwnedBy (switch i))
switch-preserves-coherence i = coherent-after (switch i)

hidden-target-hides-old : {W M : Set}
  (old target : W) (selected : M) (distinct : old ≠ target) →
  oldNowOwnedBy
    (switch (before old target selected hidden distinct right-hidden)) ≡ hidden
hidden-target-hides-old old target selected distinct = refl

visible-target-swaps-old : {W M : Set}
  (old target : W) (selected other : M)
  (distinct-workspaces : old ≠ target) (distinct-monitors : selected ≠ other) →
  oldNowOwnedBy
    (switch (before old target selected (shown other)
      distinct-workspaces (separate distinct-monitors))) ≡ shown other
visible-target-swaps-old old target selected other distinct-workspaces distinct-monitors = refl
