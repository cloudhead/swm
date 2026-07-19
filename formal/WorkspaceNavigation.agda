{-# OPTIONS --safe #-}

module formal.WorkspaceNavigation where

open import Agda.Builtin.Bool
open import Agda.Builtin.Equality
open import Agda.Builtin.Nat
open import Agda.Builtin.Maybe
open import Agda.Builtin.List

record Candidate : Set where
  constructor candidate
  field index : Nat
        visibleElsewhere occupied : Bool
open Candidate public

not : Bool → Bool
not true = false
not false = true

and : Bool → Bool → Bool
and true b = b
and false b = false

or : Bool → Bool → Bool
or true b = true
or false b = b

eligible : Bool → Candidate → Bool
eligible allowEmpty c = and (not (visibleElsewhere c)) (or allowEmpty (occupied c))

next : Bool → List Candidate → Maybe Candidate
next allowEmpty [] = nothing
next allowEmpty (c ∷ cs) with eligible allowEmpty c
... | true = just c
... | false = next allowEmpty cs

data _∈_ (x : Candidate) : List Candidate → Set where
  here : {xs : List Candidate} → x ∈ (x ∷ xs)
  there : {y : Candidate} {xs : List Candidate} → x ∈ xs → x ∈ (y ∷ xs)

-- This is stronger than membership plus eligibility: every candidate before
-- the result is accompanied by evidence that it was rejected.
data FirstSelected (allowEmpty : Bool) : List Candidate → Candidate → Set where
  selected : {c : Candidate} {cs : List Candidate} →
    eligible allowEmpty c ≡ true → FirstSelected allowEmpty (c ∷ cs) c
  skipped : {x c : Candidate} {xs : List Candidate} →
    eligible allowEmpty x ≡ false → FirstSelected allowEmpty xs c →
    FirstSelected allowEmpty (x ∷ xs) c

next-sound : (allowEmpty : Bool) (xs : List Candidate) (c : Candidate) →
  next allowEmpty xs ≡ just c → FirstSelected allowEmpty xs c
next-sound allowEmpty [] c ()
next-sound allowEmpty (x ∷ xs) c proof with eligible allowEmpty x in eq
next-sound allowEmpty (x ∷ xs) .x refl | true = selected eq
... | false = skipped eq (next-sound allowEmpty xs c proof)

first-selected-member : {allowEmpty : Bool} {xs : List Candidate} {c : Candidate} →
  FirstSelected allowEmpty xs c → c ∈ xs
first-selected-member (selected ok) = here
first-selected-member (skipped no result) = there (first-selected-member result)

first-selected-eligible : {allowEmpty : Bool} {xs : List Candidate} {c : Candidate} →
  FirstSelected allowEmpty xs c → eligible allowEmpty c ≡ true
first-selected-eligible (selected ok) = ok
first-selected-eligible (skipped no result) = first-selected-eligible result

data AllRejected (allowEmpty : Bool) : List Candidate → Set where
  none : AllRejected allowEmpty []
  reject : {c : Candidate} {cs : List Candidate} →
    eligible allowEmpty c ≡ false → AllRejected allowEmpty cs →
    AllRejected allowEmpty (c ∷ cs)

next-complete : (allowEmpty : Bool) (xs : List Candidate) →
  next allowEmpty xs ≡ nothing → AllRejected allowEmpty xs
next-complete allowEmpty [] refl = none
next-complete allowEmpty (c ∷ cs) proof with eligible allowEmpty c in eq
next-complete allowEmpty (c ∷ cs) () | true
... | false = reject eq (next-complete allowEmpty cs proof)
