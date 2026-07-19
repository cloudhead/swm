{-# OPTIONS --safe #-}

module formal.Geometry where

open import Agda.Builtin.Equality
open import Agda.Builtin.Nat hiding (_<_)

infix 4 _≤_ _<_

data _≤_ : Nat → Nat → Set where
  z≤n : {n : Nat} → zero ≤ n
  s≤s : {m n : Nat} → m ≤ n → suc m ≤ suc n

_<_ : Nat → Nat → Set
m < n = suc m ≤ n

data Positive : Nat → Set where
  positive : {n : Nat} → Positive (suc n)

≤-refl : (n : Nat) → n ≤ n
≤-refl zero = z≤n
≤-refl (suc n) = s≤s (≤-refl n)

≤-step : {m n : Nat} → m ≤ n → m ≤ suc n
≤-step z≤n = z≤n
≤-step (s≤s p) = s≤s (≤-step p)

data Order (m n : Nat) : Set where
  le : m ≤ n → Order m n
  greater : n < m → Order m n

compare : (m n : Nat) → Order m n
compare zero n = le z≤n
compare (suc m) zero = greater (s≤s z≤n)
compare (suc m) (suc n) with compare m n
... | le p = le (s≤s p)
... | greater p = greater (s≤s p)

add-positive-right : (a b : Nat) → Positive b → Positive (a + b)
add-positive-right zero b p = p
add-positive-right (suc a) b p = positive

less-add-positive : (a b : Nat) → Positive b → a < a + b
less-add-positive zero (suc b) positive = s≤s z≤n
less-add-positive (suc a) b p = s≤s (less-add-positive a b p)

minus≤left : (a b : Nat) → a - b ≤ a
minus≤left a zero = ≤-refl a
minus≤left zero (suc b) = z≤n
minus≤left (suc a) (suc b) = ≤-step (minus≤left a b)

subtract-positive-less : (a b : Nat) →
  Positive a → Positive b → a - b < a
subtract-positive-less (suc a) (suc b) positive positive = s≤s (minus≤left a b)

+-zero-right : (a : Nat) → a + zero ≡ a
+-zero-right zero = refl
+-zero-right (suc a) rewrite +-zero-right a = refl

+-suc-right : (a b : Nat) → a + suc b ≡ suc (a + b)
+-suc-right zero b = refl
+-suc-right (suc a) b rewrite +-suc-right a b = refl

minus-plus-cancel : {a b : Nat} → b ≤ a → (a - b) + b ≡ a
minus-plus-cancel {a} {zero} p = +-zero-right a
minus-plus-cancel {suc a} {suc b} (s≤s p)
  rewrite +-suc-right (a - b) b | minus-plus-cancel p = refl

record Box : Set where
  constructor box
  field x y width height : Nat
open Box public

record Output : Set where
  constructor output
  field
    output-x output-y output-width output-height : Nat
    output-width-positive : Positive output-width
    output-height-positive : Positive output-height
open Output public

minimum : Nat → Nat
minimum border = 1 + 2 * border

minimum-positive : (border : Nat) → Positive (minimum border)
minimum-positive border = positive

record LowerBound (lower : Nat) : Set where
  constructor bounded-below
  field value : Nat
        bound : lower ≤ value
open LowerBound public

clampMinimum : (lower proposed : Nat) → LowerBound lower
clampMinimum lower proposed with compare lower proposed
... | le p = bounded-below proposed p
... | greater p = bounded-below lower (≤-refl lower)

positive-above-positive : {m n : Nat} → Positive m → m ≤ n → Positive n
positive-above-positive positive (s≤s p) = positive

record MinimumSized (border : Nat) (b : Box) : Set where
  constructor minimum-sized
  field
    width-respects-minimum : minimum border ≤ width b
    height-respects-minimum : minimum border ≤ height b
open MinimumSized public

record AxisOverlap
  (start size bound-start bound-size : Nat) : Set where
  constructor axis-overlap
  field
    starts-before-far-edge : start < bound-start + bound-size
    ends-after-near-edge : bound-start < start + size
open AxisOverlap public

record Overlaps (b : Box) (bounds : Output) : Set where
  constructor overlaps
  field
    horizontal : AxisOverlap (x b) (width b) (output-x bounds) (output-width bounds)
    vertical : AxisOverlap (y b) (height b) (output-y bounds) (output-height bounds)
open Overlaps public

record ClampedAxis
  (size bound-start bound-size : Nat) : Set where
  constructor clamped-axis
  field
    clamped-start : Nat
    clamped-overlap : AxisOverlap clamped-start size bound-start bound-size
open ClampedAxis public

finishClamp :
  (candidate size bound-start bound-size : Nat) →
  candidate < bound-start + bound-size → Positive size → Positive bound-size →
  ClampedAxis size bound-start bound-size
finishClamp candidate size bound-start bound-size before-far size-positive bound-positive
  with compare (candidate + size) bound-start
... | le ends-before = clamped-axis bound-start
  (axis-overlap
    (less-add-positive bound-start bound-size bound-positive)
    (less-add-positive bound-start size size-positive))
... | greater ends-after = clamped-axis candidate (axis-overlap before-far ends-after)

-- The two comparisons occur in the same order as swm_box_apply_bounds: first
-- move a box lying beyond the far edge, then move one lying beyond the near
-- edge.  Natural coordinates require no signed-arithmetic assumptions here.
clampAxis :
  (start size bound-start bound-size : Nat) → Positive size → Positive bound-size →
  ClampedAxis size bound-start bound-size
clampAxis start size bound-start bound-size size-positive bound-positive
  with compare (bound-start + bound-size) start
... | le outside-far = finishClamp
  ((bound-start + bound-size) - size) size bound-start bound-size
  (subtract-positive-less
    (bound-start + bound-size) size
    (add-positive-right bound-start bound-size bound-positive) size-positive)
  size-positive bound-positive
... | greater inside-far =
  finishClamp start size bound-start bound-size inside-far size-positive bound-positive

record BoundedBox (bounds : Output) (border : Nat) : Set where
  constructor bounded-box
  field
    geometry : Box
    respects-minimum : MinimumSized border geometry
    overlaps-output : Overlaps geometry bounds
open BoundedBox public

applyBounds : (b : Box) (bounds : Output) (border : Nat) → BoundedBox bounds border
applyBounds b bounds border =
  let w = clampMinimum (minimum border) (width b)
      h = clampMinimum (minimum border) (height b)
      wp = positive-above-positive (minimum-positive border) (bound w)
      hp = positive-above-positive (minimum-positive border) (bound h)
      horizontal-result =
        clampAxis (x b) (value w) (output-x bounds) (output-width bounds)
          wp (output-width-positive bounds)
      vertical-result =
        clampAxis (y b) (value h) (output-y bounds) (output-height bounds)
          hp (output-height-positive bounds)
      result = box
        (clamped-start horizontal-result) (clamped-start vertical-result)
        (value w) (value h)
  in bounded-box result (minimum-sized (bound w) (bound h))
       (overlaps
         (clamped-overlap horizontal-result)
         (clamped-overlap vertical-result))

apply-bounds-overlaps : (b : Box) (bounds : Output) (border : Nat) →
  Overlaps (geometry (applyBounds b bounds border)) bounds
apply-bounds-overlaps b bounds border = overlaps-output (applyBounds b bounds border)

apply-bounds-minimum : (b : Box) (bounds : Output) (border : Nat) →
  MinimumSized border (geometry (applyBounds b bounds border))
apply-bounds-minimum b bounds border = respects-minimum (applyBounds b bounds border)

data Delta : Set where
  grow shrink : Nat → Delta

invert : Delta → Delta
invert (grow n) = shrink n
invert (shrink n) = grow n

adjust : Nat → Delta → Nat
adjust n (grow amount) = n + amount
adjust n (shrink amount) = n - amount

data Anchor : Set where
  near far : Anchor

record Edges : Set where
  constructor edges
  field horizontal-edge vertical-edge : Anchor
open Edges public

orientedDelta : Anchor → Delta → Delta
orientedDelta near delta = invert delta
orientedDelta far delta = delta

reposition : Anchor → Nat → Nat → Nat → Nat
reposition near old-start old-size new-size = (old-start + old-size) - new-size
reposition far old-start old-size new-size = old-start

record ResizeResult (input : Box) (dx dy : Delta) (selected : Edges) (border : Nat) : Set where
  constructor resized
  field
    resized-geometry : Box
    resized-minimum : MinimumSized border resized-geometry
    horizontal-rule :
      x resized-geometry ≡ reposition (horizontal-edge selected)
        (x input) (width input) (width resized-geometry)
    vertical-rule :
      y resized-geometry ≡ reposition (vertical-edge selected)
        (y input) (height input) (height resized-geometry)
open ResizeResult public

resize : (b : Box) (dx dy : Delta) (selected : Edges) (border : Nat) →
  ResizeResult b dx dy selected border
resize b dx dy selected border =
  let w = clampMinimum (minimum border)
        (adjust (width b) (orientedDelta (horizontal-edge selected) dx))
      h = clampMinimum (minimum border)
        (adjust (height b) (orientedDelta (vertical-edge selected) dy))
      result = box
        (reposition (horizontal-edge selected) (x b) (width b) (value w))
        (reposition (vertical-edge selected) (y b) (height b) (value h))
        (value w) (value h)
  in resized result (minimum-sized (bound w) (bound h)) refl refl

data AxisFits : Anchor → Nat → Nat → Nat → Set where
  far-fits : {old-start old-size new-size : Nat} →
    AxisFits far old-start old-size new-size
  near-fits : {old-start old-size new-size : Nat} →
    new-size ≤ old-start + old-size →
    AxisFits near old-start old-size new-size

data FixedAxis : Anchor → Nat → Nat → Nat → Nat → Set where
  fixed-near : {old-start old-size new-start new-size : Nat} →
    new-start + new-size ≡ old-start + old-size →
    FixedAxis near old-start old-size new-start new-size
  fixed-far : {old-start old-size new-start new-size : Nat} →
    new-start ≡ old-start →
    FixedAxis far old-start old-size new-start new-size

reposition-fixes-axis :
  (anchor : Anchor) (old-start old-size new-size : Nat) →
  AxisFits anchor old-start old-size new-size →
  FixedAxis anchor old-start old-size
    (reposition anchor old-start old-size new-size) new-size
reposition-fixes-axis near old-start old-size new-size (near-fits fits) =
  fixed-near (minus-plus-cancel fits)
reposition-fixes-axis far old-start old-size new-size far-fits = fixed-far refl

record FixedEdges
  (old new : Box) (selected : Edges) : Set where
  constructor fixed-edges
  field
    horizontal-fixed : FixedAxis (horizontal-edge selected)
      (x old) (width old) (x new) (width new)
    vertical-fixed : FixedAxis (vertical-edge selected)
      (y old) (height old) (y new) (height new)

resize-preserves-fixed-edges :
  (b : Box) (dx dy : Delta) (selected : Edges) (border : Nat) →
  let result = resized-geometry (resize b dx dy selected border)
  in AxisFits (horizontal-edge selected) (x b) (width b) (width result) →
     AxisFits (vertical-edge selected) (y b) (height b) (height result) →
     FixedEdges b result selected
resize-preserves-fixed-edges b dx dy selected border horizontal-fits vertical-fits =
  fixed-edges
    (reposition-fixes-axis (horizontal-edge selected)
      (x b) (width b)
      (width (resized-geometry (resize b dx dy selected border))) horizontal-fits)
    (reposition-fixes-axis (vertical-edge selected)
      (y b) (height b)
      (height (resized-geometry (resize b dx dy selected border))) vertical-fits)
