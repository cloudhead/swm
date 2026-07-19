-- Dependency-free model of swm's pure window-management policy.
-- The constant 32 is SWM_SLICE from swm.h.
{-# OPTIONS --safe #-}

module formal.Core where

open import Agda.Builtin.Equality
open import Agda.Builtin.Nat
open import Agda.Builtin.Bool

data Side : Set where
  left top right bottom : Side

data Command : Set where
  masterShrink masterGrow masterAdd masterDel : Command
  stackInc stackDec stackReset flipLayout : Command

record Stack : Set where
  constructor stack
  field msize mwin stacks : Nat
open Stack public

record Result : Set where
  constructor result
  field state : Stack
        side  : Side
open Result public

pred1 : Nat → Nat
pred1 zero          = zero
pred1 (suc zero)    = 1
pred1 (suc (suc n)) = suc n

-- Increment while below the cap.  On inputs in 1..31 this is extensionally
-- equal to the guarded increment in C; its structural form makes the bound
-- transparent to the termination checker.
growTo : Nat → Nat → Nat
growTo zero n = n
growTo (suc limit) zero = 1
growTo (suc limit) (suc n) = suc (growTo limit n)

grow31 : Nat → Nat
grow31 zero = 1
grow31 (suc n) = suc (growTo 30 n)

flip : Side → Side
flip left   = right
flip right  = left
flip top    = bottom
flip bottom = top

resetSide : Side → Side
resetSide right  = left
resetSide bottom = top
resetSide s      = s

-- Total accepted-command path of swm_stack_configure.
configure : Command → Stack → Side → Result
configure masterShrink (stack z w k) d = result (stack (pred1 z) w k) d
configure masterGrow   (stack z w k) d = result (stack (grow31 z) w k) d
configure masterAdd    (stack z w k) d = result (stack z (suc w) k) d
configure masterDel    (stack z zero k) d = result (stack z zero k) d
configure masterDel    (stack z (suc w) k) d = result (stack z w k) d
configure stackInc     (stack z w k) d = result (stack z w (suc k)) d
configure stackDec     (stack z w k) d = result (stack z w (pred1 k)) d
configure stackReset   s d = result (stack 16 1 1) (resetSide d)
configure flipLayout   s d = result s (flip d)

data Positive : Nat → Set where
  positive : {n : Nat} → Positive (suc n)

data AtMost : Nat → Nat → Set where
  z≤n : {n : Nat} → AtMost zero n
  s≤s : {m n : Nat} → AtMost m n → AtMost (suc m) (suc n)

AtMost31 : Nat → Set
AtMost31 n = AtMost n 31

weaken : {m n : Nat} → AtMost m n → AtMost m (suc n)
weaken z≤n = z≤n
weaken (s≤s p) = s≤s (weaken p)

record Valid (s : Stack) : Set where
  constructor valid
  field
    master-positive : Positive (msize s)
    master-bounded  : AtMost31 (msize s)
    stacks-positive : Positive (stacks s)

pred1-positive : {n : Nat} → Positive n → Positive (pred1 n)
pred1-positive {suc zero} positive = positive
pred1-positive {suc (suc n)} positive = positive

pred1-bounded : {n : Nat} → AtMost31 n → AtMost31 (pred1 n)
pred1-bounded {zero} z≤n = z≤n
pred1-bounded {suc zero} (s≤s z≤n) = s≤s z≤n
pred1-bounded {suc (suc n)} (s≤s (s≤s p)) = s≤s (weaken p)

growTo-positive : (limit : Nat) {n : Nat} → Positive n → Positive (growTo limit n)
growTo-positive zero p = p
growTo-positive (suc limit) positive = positive

growTo-bounded : (limit : Nat) {n : Nat} →
  AtMost n limit → AtMost (growTo limit n) limit
growTo-bounded zero {zero} z≤n = z≤n
growTo-bounded (suc limit) {zero} z≤n = s≤s z≤n
growTo-bounded (suc limit) {suc n} (s≤s p) = s≤s (growTo-bounded limit p)

grow31-positive : {n : Nat} → Positive n → Positive (grow31 n)
grow31-positive positive = positive

grow31-bounded : {n : Nat} → AtMost31 n → AtMost31 (grow31 n)
grow31-bounded {zero} z≤n = s≤s z≤n
grow31-bounded {suc n} (s≤s p) = s≤s (growTo-bounded 30 p)

-- Every accepted stack command preserves the layout's parameter invariants.
configure-preserves-valid :
  (c : Command) (s : Stack) (d : Side) → Valid s → Valid (state (configure c s d))
configure-preserves-valid masterShrink (stack z w k) d (valid p b q) =
  valid (pred1-positive p) (pred1-bounded b) q
configure-preserves-valid masterGrow (stack z w k) d (valid p b q) =
  valid (grow31-positive p) (grow31-bounded b) q
configure-preserves-valid masterAdd (stack z w k) d (valid p b q) = valid p b q
configure-preserves-valid masterDel (stack z zero k) d (valid p b q) = valid p b q
configure-preserves-valid masterDel (stack z (suc w) k) d (valid p b q) = valid p b q
configure-preserves-valid stackInc (stack z w k) d (valid p b q) = valid p b positive
configure-preserves-valid stackDec (stack z w k) d (valid p b q) =
  valid p b (pred1-positive q)
configure-preserves-valid stackReset s d v =
  valid positive
    (s≤s (s≤s (s≤s (s≤s (s≤s (s≤s (s≤s (s≤s
      (s≤s (s≤s (s≤s (s≤s (s≤s (s≤s (s≤s (s≤s z≤n))))))))))))))))
    positive
configure-preserves-valid flipLayout s d v = v

flip-involutive : (d : Side) → flip (flip d) ≡ d
flip-involutive left = refl
flip-involutive top = refl
flip-involutive right = refl
flip-involutive bottom = refl

flip-keeps-state : (s : Stack) (d : Side) → state (configure flipLayout s d) ≡ s
flip-keeps-state s d = refl

reset-is-canonical : (s : Stack) (d : Side) →
  state (configure stackReset s d) ≡ stack 16 1 1
reset-is-canonical s d = refl

record WorkspaceFlags : Set where
  constructor flags
  field active urgent hidden : Bool
open WorkspaceFlags public

not : Bool → Bool
not true = false
not false = true

_and_ : Bool → Bool → Bool
true and b = b
false and b = false

workspaceFlags : Bool → Bool → Bool → WorkspaceFlags
workspaceFlags a occupied u = flags a u (not a and not occupied)

active-not-hidden : (occupied urgent : Bool) →
  hidden (workspaceFlags true occupied urgent) ≡ false
active-not-hidden occupied urgent = refl

empty-inactive-hidden : (urgent : Bool) →
  hidden (workspaceFlags false false urgent) ≡ true
empty-inactive-hidden urgent = refl

occupied-not-hidden : (active urgent : Bool) →
  hidden (workspaceFlags active true urgent) ≡ false
occupied-not-hidden true urgent = refl
occupied-not-hidden false urgent = refl

urgency-preserved : (active occupied urgent : Bool) →
  WorkspaceFlags.urgent (workspaceFlags active occupied urgent) ≡ urgent
urgency-preserved active occupied urgent = refl
