{-# OPTIONS --safe #-}

module formal.LayoutCycle where

open import Agda.Builtin.Bool
open import Agda.Builtin.Equality
open import Agda.Builtin.Nat
open import Agda.Builtin.Maybe
open import Agda.Builtin.List

record Layout : Set where
  constructor layout
  field index : Nat
        enabled : Bool
open Layout public

-- The input list is the cycle array rotated to begin just after current.
-- Searching it therefore models swm_next_layout's wraparound order.
nextLayout : List Layout → Maybe Layout
nextLayout [] = nothing
nextLayout (x ∷ xs) with enabled x
... | true = just x
... | false = nextLayout xs

data _∈_ (x : Layout) : List Layout → Set where
  here : {xs : List Layout} → x ∈ (x ∷ xs)
  there : {y : Layout} {xs : List Layout} → x ∈ xs → x ∈ (y ∷ xs)

data FirstEnabled : List Layout → Layout → Set where
  found : {x : Layout} {xs : List Layout} →
    enabled x ≡ true → FirstEnabled (x ∷ xs) x
  skipped : {x y : Layout} {xs : List Layout} →
    enabled x ≡ false → FirstEnabled xs y → FirstEnabled (x ∷ xs) y

cycle-sound : (xs : List Layout) (x : Layout) →
  nextLayout xs ≡ just x → FirstEnabled xs x
cycle-sound [] x ()
cycle-sound (y ∷ ys) x proof with enabled y in eq
cycle-sound (y ∷ ys) .y refl | true = found eq
... | false = skipped eq (cycle-sound ys x proof)

first-enabled-member : {xs : List Layout} {x : Layout} → FirstEnabled xs x → x ∈ xs
first-enabled-member (found ok) = here
first-enabled-member (skipped no result) = there (first-enabled-member result)

first-enabled-ok : {xs : List Layout} {x : Layout} →
  FirstEnabled xs x → enabled x ≡ true
first-enabled-ok (found ok) = ok
first-enabled-ok (skipped no result) = first-enabled-ok result

data NoneEnabled : List Layout → Set where
  none : NoneEnabled []
  disabled : {x : Layout} {xs : List Layout} →
    enabled x ≡ false → NoneEnabled xs → NoneEnabled (x ∷ xs)

cycle-complete : (xs : List Layout) →
  nextLayout xs ≡ nothing → NoneEnabled xs
cycle-complete [] refl = none
cycle-complete (x ∷ xs) proof with enabled x in eq
cycle-complete (x ∷ xs) () | true
... | false = disabled eq (cycle-complete xs proof)
