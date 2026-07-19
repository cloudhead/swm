{-# OPTIONS --safe #-}

module formal.StackLayout where

open import Agda.Builtin.Equality
open import Agda.Builtin.Nat

data Positive : Nat → Set where
  positive : {n : Nat} → Positive (suc n)

data AtMost : Nat → Nat → Set where
  z≤n : {n : Nat} → AtMost zero n
  s≤s : {m n : Nat} → AtMost m n → AtMost (suc m) (suc n)

cong : {A B : Set} {x y : A} (f : A → B) → x ≡ y → f x ≡ f y
cong f refl = refl

subst : {A : Set} (P : A → Set) {x y : A} → x ≡ y → P x → P y
subst P refl px = px

trans : {A : Set} {x y z : A} → x ≡ y → y ≡ z → x ≡ z
trans refl yz = yz

+-zero-right : (n : Nat) → n + zero ≡ n
+-zero-right zero = refl
+-zero-right (suc n) rewrite +-zero-right n = refl

data Vec (A : Set) : Nat → Set where
  []  : Vec A zero
  _::_ : {n : Nat} → A → Vec A n → Vec A (suc n)

infixr 5 _::_ _++_

_++_ : {A : Set} {m n : Nat} → Vec A m → Vec A n → Vec A (m + n)
[] ++ ys = ys
(x :: xs) ++ ys = x :: (xs ++ ys)

replicate : {A : Set} → A → (n : Nat) → Vec A n
replicate x zero = []
replicate x (suc n) = x :: replicate x n

sum : {n : Nat} → Vec Nat n → Nat
sum [] = zero
sum (x :: xs) = x + sum xs

data AllPositive : {n : Nat} → Vec Nat n → Set where
  [] : AllPositive []
  _::_ : {n x : Nat} {xs : Vec Nat n} →
    Positive x → AllPositive xs → AllPositive (x :: xs)

record PositivePartition (total columns : Nat) : Set where
  constructor positive-partition
  field
    parts : Vec Nat columns
    all-positive : AllPositive parts
    covers : sum parts ≡ total
open PositivePartition public

-- Construct a nonempty row count for every column.  The construction is
-- independent of division: columns <= clients is exactly the fact needed to
-- make every divisor positive and to account for every client.
positivePartition : (total columns : Nat) →
  Positive columns → AtMost columns total → PositivePartition total columns
positivePartition zero (suc columns) positive ()
positivePartition (suc total) (suc zero) positive (s≤s z≤n) =
  positive-partition (suc total :: []) (positive :: [])
    (cong suc (+-zero-right total))
positivePartition (suc total) (suc (suc columns)) positive (s≤s bounded)
  with positivePartition total (suc columns) positive bounded
... | positive-partition rows rows-positive coverage =
  positive-partition (1 :: rows) (positive :: rows-positive) (cong suc coverage)

record Box : Set where
  constructor box
  field x y width height : Nat
open Box public

record SafeDiv (numerator denominator : Nat) : Set where
  constructor division
  field denominator-positive : Positive denominator

data SafeRowDivisions : {n : Nat} → Vec Nat n → Set where
  [] : SafeRowDivisions []
  _::_ : {n row : Nat} {rows : Vec Nat n} →
    SafeDiv 0 row → SafeRowDivisions rows → SafeRowDivisions (row :: rows)

rowDivisionsSafe : {n : Nat} {rows : Vec Nat n} →
  AllPositive rows → SafeRowDivisions rows
rowDivisionsSafe [] = []
rowDivisionsSafe (p :: ps) = division p :: rowDivisionsSafe ps

record ClientSplit (requested total : Nat) : Set where
  constructor client-split
  field
    master-clients stack-clients : Nat
    accounts-for-all-clients : master-clients + stack-clients ≡ total
open ClientSplit public

-- This is MIN(requested, total), paired with the remaining clients.
splitClients : (requested total : Nat) → ClientSplit requested total
splitClients zero total = client-split zero total refl
splitClients (suc requested) zero = client-split zero zero refl
splitClients (suc requested) (suc total) with splitClients requested total
... | client-split master stack coverage =
  client-split (suc master) stack (cong suc coverage)

minimum : Nat → Nat → Nat
minimum zero n = zero
minimum (suc m) zero = zero
minimum (suc m) (suc n) = suc (minimum m n)

minimum-positive : {m n : Nat} → Positive m → Positive n → Positive (minimum m n)
minimum-positive positive positive = positive

minimum-at-most-right : (m n : Nat) → AtMost (minimum m n) n
minimum-at-most-right zero n = z≤n
minimum-at-most-right (suc m) zero = z≤n
minimum-at-most-right (suc m) (suc n) = s≤s (minimum-at-most-right m n)

data StackColumns : Nat → Nat → Set where
  no-stack : {requested : Nat} → StackColumns zero requested
  stack-columns : {clients requested : Nat} →
    (columns : Nat) →
    columns ≡ minimum requested clients →
    (partition : PositivePartition clients columns) →
    SafeRowDivisions (parts partition) →
    StackColumns clients requested

makeStackColumns : (clients requested : Nat) → Positive requested →
  StackColumns clients requested
makeStackColumns zero requested requested-positive = no-stack
makeStackColumns (suc clients) requested requested-positive =
  let columns = minimum requested (suc clients)
      columns-positive = minimum-positive requested-positive positive
      partition = positivePartition (suc clients) columns columns-positive
        (minimum-at-most-right requested (suc clients))
  in stack-columns columns refl partition (rowDivisionsSafe (all-positive partition))

record LayoutPlan (masterRequest stackRequest count : Nat) : Set where
  constructor layout-plan
  field
    split : ClientSplit masterRequest count
    stacks : StackColumns (stack-clients split) stackRequest
open LayoutPlan public

planLayout : (masterRequest stackRequest count : Nat) → Positive stackRequest →
  LayoutPlan masterRequest stackRequest count
planLayout masterRequest stackRequest count stack-positive =
  let client-split-result = splitClients masterRequest count
  in layout-plan client-split-result
       (makeStackColumns (stack-clients client-split-result) stackRequest stack-positive)

-- Flattening the row plan is the structural counterpart of the C client loop.
-- Its index is the sum already proved equal to the number of stack clients.
flattenRows : {columns : Nat} → Box → (rows : Vec Nat columns) →
  Vec Box (sum rows)
flattenRows area [] = []
flattenRows area (row :: rows) = replicate area row ++ flattenRows area rows

traversePlan : {stackRequest stackCount : Nat} →
  (area : Box) (masterCount : Nat) → StackColumns stackCount stackRequest →
  Vec Box (masterCount + stackCount)
traversePlan area masterCount no-stack
  rewrite +-zero-right masterCount = replicate area masterCount
traversePlan area masterCount (stack-columns columns effective partition safe) =
  subst (Vec Box) (cong (masterCount +_) (covers partition))
    (replicate area masterCount ++ flattenRows area (parts partition))

layout : (area : Box) (masterRequest stackRequest count : Nat) →
  Positive stackRequest → Vec Box count
layout area masterRequest stackRequest count stack-positive
  with planLayout masterRequest stackRequest count stack-positive
... | layout-plan (client-split master stack coverage) stack-plan
  = subst (Vec Box) coverage (traversePlan area master stack-plan)

layout-produces-count : (area : Box) (masterRequest stackRequest count : Nat) →
  Positive stackRequest → Vec Box count
layout-produces-count = layout

-- Width partitions are constructed, rather than accepted with a caller-supplied
-- coverage proof.  Zero-width columns are allowed just as in the C layout when
-- an output is narrower than its column count.
zeros : (n : Nat) → Vec Nat n
zeros zero = []
zeros (suc n) = zero :: zeros n

sum-zeros : (n : Nat) → sum (zeros n) ≡ zero
sum-zeros zero = refl
sum-zeros (suc n) = sum-zeros n

record WidthPartition (total columns : Nat) : Set where
  constructor width-partition
  field
    widths : Vec Nat columns
    width-coverage : sum widths ≡ total
open WidthPartition public

partitionWidth : (total columns : Nat) → Positive columns →
  WidthPartition total columns
partitionWidth total (suc columns) positive =
  width-partition (total :: zeros columns)
    (trans (cong (total +_) (sum-zeros columns)) (+-zero-right total))

partition-covers : {total columns : Nat} (p : WidthPartition total columns) →
  sum (widths p) ≡ total
partition-covers = width-coverage
