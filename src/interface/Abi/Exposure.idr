-- SPDX-License-Identifier: MPL-2.0
-- Copyright (c) 2026 Jonathan D.A. Jewell (metadatastician) <j.d.a.jewell@open.ac.uk>
||| Evil Weevil ABI 1.2 — the model of the target's knowledge (mind slice 2, ADR-0014).
|||
||| The specification of the `ABI 1.2` code in `src/core/kernel.zig`: one first-order
||| belief about a target T, "T knows where I am", called EXPOSURE; one candidate that
||| acts on its absence, STALK; one tell, CAUGHT. The kernel implements what is written
||| here; eight pinned model digests, asserted by both null hosts, make a divergence
||| loud, and the ten 1.0 and 1.1 digests are unchanged.
|||
||| == The rules this module makes checkable
|||
||| 1. Integer only. Every quantity is a `Nat` or a `Bool`.
||| 2. EXPOSURE IS A BELIEF, NOT AFFECT. Only evidence moves it: the host's cue or the
|||    agent's own attack, and the lapse of its lifetime when neither arrives. The type of
|||    `exposedAfter` is the first half of the proof — no reservoir is an argument — and
|||    `exposureTable` and `causeIffChange` are the second.
||| 3. NO CHANGE WITHOUT THE CAPABILITY. Without `EE_CAP_OBSERVATION_CUES` exposure is
|||    false (`noCapabilityNoExposure`), no change has a cause (`noCapabilityNoCause`),
|||    and stalk is not a candidate (`noCapabilityRankingIsTheMindRanking`).
||| 4. WITH EXPOSURE TRUE THE RANKING IS 1.1's (`exposedRankingIsTheMindRanking`), for
|||    every situation, whatever the capability, the contact or the token.
||| 5. THE LIFETIME IS BOUNDED (`lifetimeBounded`, `quietTicksBounded`): from any state,
|||    garbage included, a belief with no evidence ends within `exposureTtl` ticks, and
|||    the false-exposure fixture measures exactly that many (`falseExposureLastsTheLifetime`).
|||
||| == What is general and what is an instance
|||
||| General: rules 2..5 above, and that a second CAUGHT needs the belief to lapse first.
||| By instance: the rankings of the stalker's three moments, and the order of the
||| weights that make concealment defer the strike without holding against reload,
||| flight or an engagement already begun. Same convention as `Abi.Affect`.

module Abi.Exposure

import Abi.Types
import Abi.Utility
import Abi.Modes
import Abi.Affect
import Data.Nat
import Data.So

%default total

--------------------------------------------------------------------------------
-- The constants (policy, not ABI: none of these is in ee.h)
--------------------------------------------------------------------------------
-- Each is the kernel constant of the same name, in SCREAMING_CASE there.

||| Ticks the belief outlives its last evidence: 1.5 s at 60 Hz. A counter, not a leak.
public export
exposureTtl : Nat
exposureTtl = 90

||| Unseen, the agent engages only this close (0.5); out to attack range it stalks.
public export
strikeRange : Nat
strikeRange = 32768

||| Stalk's weight (0.85).
public export
utilityStalk : Nat
utilityStalk = 55705

||| A stalker's pace (0.5). Not in any theorem: it moves the stalker's pinned digest.
public export
stalkSpeed : Nat
stalkSpeed = 32768

--------------------------------------------------------------------------------
-- The belief
--------------------------------------------------------------------------------

||| Exposure as a tick closes. `cap`: the host declared the capability. `ex`: the belief
||| as the tick opened. `live`: its lifetime has not run out (the counter is above zero).
||| `cue`: a visible contact observes the agent now. `att`: the agent's body attacked.
|||
||| Nothing else is an argument — no reservoir, no score, no token. That is rule 2.
public export
exposedAfter : (cap, ex, live, cue, att : Bool) -> Bool
exposedAfter False _ _ _ _ = False
exposedAfter True ex live cue att = cue || (att || (ex && live))

||| The belief as the trace reports it opened: false without the capability.
public export
exposedBefore : (cap, ex : Bool) -> Bool
exposedBefore cap ex = cap && ex

public export
data ExposureCause = ENone | ECue | EOwnAttack | EExpiry

public export
causeCode : ExposureCause -> Nat
causeCode ENone = 0
causeCode ECue = 1
causeCode EOwnAttack = 2
causeCode EExpiry = 3

public export
eqCause : ExposureCause -> ExposureCause -> Bool
eqCause ENone ENone = True
eqCause ECue ECue = True
eqCause EOwnAttack EOwnAttack = True
eqCause EExpiry EExpiry = True
eqCause _ _ = False

||| The codes are the wire's (`EE_EXPOSURE_CAUSE_*`).
export
causesMatchTheWire :
  ( causeCode ENone, causeCode ECue, causeCode EOwnAttack, causeCode EExpiry )
  = ( Abi.Types.exposureCauseNone, Abi.Types.exposureCauseCue
    , Abi.Types.exposureCauseOwnAttack, Abi.Types.exposureCauseExpiry )
causesMatchTheWire = Refl

||| Why the belief changed: NONE when it did not; otherwise the cue if there was one,
||| else the own attack, else expiry. The kernel's `stepExposure`.
public export
causeOf : (cap, ex, live, cue, att : Bool) -> ExposureCause
causeOf False _ _ _ _ = ENone
causeOf True ex live cue att =
  if exposedAfter True ex live cue att == ex
     then ENone
     else if cue then ECue else if att then EOwnAttack else EExpiry

||| RULE 3: without the capability the belief is false, whatever the struct held and
||| whatever the host flagged.
export
noCapabilityNoExposure : (ex, live, cue, att : Bool) -> exposedAfter False ex live cue att = False
noCapabilityNoExposure _ _ _ _ = Refl

export
noCapabilityNoCause : (ex, live, cue, att : Bool) -> causeOf False ex live cue att = ENone
noCapabilityNoCause _ _ _ _ = Refl

||| A CHANGE HAS A CAUSE AND NO CHANGE HAS NONE — for every input. The cause is NONE
||| exactly when the belief closed as it opened.
export
causeIffChange : (cap, ex, live, cue, att : Bool) ->
  eqCause (causeOf cap ex live cue att) ENone
  = (exposedAfter cap ex live cue att == exposedBefore cap ex)
causeIffChange False _ _ _ _ = Refl
causeIffChange True False False False False = Refl
causeIffChange True False False False True = Refl
causeIffChange True False False True False = Refl
causeIffChange True False False True True = Refl
causeIffChange True False True False False = Refl
causeIffChange True False True False True = Refl
causeIffChange True False True True False = Refl
causeIffChange True False True True True = Refl
causeIffChange True True False False False = Refl
causeIffChange True True False False True = Refl
causeIffChange True True False True False = Refl
causeIffChange True True False True True = Refl
causeIffChange True True True False False = Refl
causeIffChange True True True False True = Refl
causeIffChange True True True True False = Refl
causeIffChange True True True True True = Refl

||| RULE 2, AS A TABLE: the inputs are four Booleans, so these sixteen rows are every
||| case there is. Read down the columns: the belief RISES only on a row with a cue or an
||| attack, and its cause names that evidence; it FALLS only on the one row with neither
||| and a spent lifetime, and the cause is expiry; every other row keeps it, with NONE.
|||
|||   (ex, live, cue, att)  ->  (exposed after, cause)
public export
row : (ex, live, cue, att : Bool) -> (Bool, ExposureCause)
row ex live cue att = (exposedAfter True ex live cue att, causeOf True ex live cue att)

export
exposureTable :
  [ row False False False False, row False False False True
  , row False False True False,  row False False True True
  , row False True False False,  row False True False True
  , row False True True False,   row False True True True
  , row True False False False,  row True False False True
  , row True False True False,   row True False True True
  , row True True False False,   row True True False True
  , row True True True False,    row True True True True ]
  = [ (False, ENone), (True, EOwnAttack), (True, ECue), (True, ECue)
    , (False, ENone), (True, EOwnAttack), (True, ECue), (True, ECue)
    , (False, EExpiry), (True, ENone), (True, ENone), (True, ENone)
    , (True, ENone), (True, ENone), (True, ENone), (True, ENone) ]
exposureTable = Refl

||| Without evidence an unexposed agent stays unexposed, under any capability.
export
riseNeedsEvidence : (cap, live : Bool) -> exposedAfter cap False live False False = False
riseNeedsEvidence False _ = Refl
riseNeedsEvidence True _ = Refl

||| A belief that ends, ends by expiry: no cue, no attack, and a spent lifetime.
export
fallIsExpiry : (live, cue, att : Bool) -> exposedAfter True True live cue att = False ->
  (live, cue, att) = (False, False, False)
fallIsExpiry False False False Refl = Refl
fallIsExpiry False False True Refl impossible
fallIsExpiry False True _ Refl impossible
fallIsExpiry True False False Refl impossible
fallIsExpiry True False True Refl impossible
fallIsExpiry True True _ Refl impossible

--------------------------------------------------------------------------------
-- The lifetime
--------------------------------------------------------------------------------

||| The counter as the tick closes. Evidence restarts it at `exposureTtl`; otherwise a
||| belief held as the tick opened counts down (`pred`), and no belief holds nothing.
||| The opening counter is CLAMPED to the lifetime: the kernel does the same to whatever
||| a host left in `ee_agent.flags` bits 8..15.
public export
leftAfter : (ex : Bool) -> (left : Nat) -> (cue, att : Bool) -> Nat
leftAfter ex l cue att =
  if cue || att
     then Abi.Exposure.exposureTtl
     else if ex then pred (clampTo Abi.Exposure.exposureTtl l) else 0

predShrinks : (n : Nat) -> LTE (pred n) n
predShrinks Z = LTEZero
predShrinks (S k) = lteStepRight (lteSelf k)

||| RULE 5: the counter never exceeds the lifetime, from any state at all.
export
lifetimeBounded : (ex : Bool) -> (l : Nat) -> (cue, att : Bool) ->
  LTE (leftAfter ex l cue att) Abi.Exposure.exposureTtl
lifetimeBounded ex l True att = lteSelf Abi.Exposure.exposureTtl
lifetimeBounded ex l False True = lteSelf Abi.Exposure.exposureTtl
lifetimeBounded True l False False =
  lteChain (predShrinks (clampTo Abi.Exposure.exposureTtl l)) (clampInRange Abi.Exposure.exposureTtl l)
lifetimeBounded False l False False = LTEZero

||| Ticks on which the belief is held with NO evidence, starting from a counter `left`:
||| each such tick spends one unit, and at zero the belief expires.
public export
quietTicks : (fuel : Nat) -> (left : Nat) -> Nat
quietTicks Z _ = 0
quietTicks (S f) Z = 0
quietTicks (S f) (S k) = S (quietTicks f k)

||| No quiet run outlasts its counter — so, with `lifetimeBounded`, none outlasts the
||| lifetime. A belief without evidence always ends.
export
quietTicksBounded : (f, l : Nat) -> LTE (quietTicks f l) l
quietTicksBounded Z _ = LTEZero
quietTicksBounded (S f) Z = LTEZero
quietTicksBounded (S f) (S k) = LTESucc (quietTicksBounded f k)

||| "False exposure": the target glances once and never again. After the cue tick the
||| belief is held for exactly the lifetime with no cue — the fixture measures 90.
export
falseExposureLastsTheLifetime : quietTicks 200 Abi.Exposure.exposureTtl = 90
falseExposureLastsTheLifetime = Refl

--------------------------------------------------------------------------------
-- The candidate
--------------------------------------------------------------------------------

||| A ranked candidate: one of the five 1.1 modes, or stalk (mode code 5 on the wire).
public export
data Candidate = Base Mode | Stalk

||| Stalk is a candidate when the belief the DECISION uses is concealment, the host
||| declared the capability, a target is visible, and the token is not afraid. Fear
||| removes the candidate; nothing makes the agent believe it is unseen.
public export
stalkEligible : (exposedNow, cap, contact, afraid : Bool) -> Bool
stalkEligible exposedNow cap contact afraid = not exposedNow && (cap && (contact && not afraid))

||| Stalk's score: its weight beyond strike range, nothing inside it.
public export
stalkScore : (eligible : Bool) -> (distance : Nat) -> Nat
stalkScore e d = if e && Abi.Exposure.strikeRange < d then Abi.Exposure.utilityStalk else 0

||| Stalk is the last candidate in tie-break order, so it enters the 1.1 ranking by the
||| same stable rule: it passes a mode only if strictly greater.
public export
insertStalk : (sc : Nat) -> MindSituation -> List Candidate -> List Candidate
insertStalk sc s [] = [Stalk]
insertStalk sc s (Base m :: xs) =
  if sc > mindScore m s then Stalk :: Base m :: xs else Base m :: insertStalk sc s xs
insertStalk sc s (Stalk :: xs) = Stalk :: xs

||| THE RANKING the 1.2 trace publishes.
public export
rankWithModel : (eligible : Bool) -> MindSituation -> List Candidate
rankWithModel False s = map Base (rankModes s)
rankWithModel True s = insertStalk (stalkScore True (msDistance s)) s (map Base (rankModes s))

||| RULE 4: WITH EXPOSURE TRUE THE RANKING IS 1.1's — for every situation, capability,
||| contact and token.
export
exposedRankingIsTheMindRanking : (cap, contact, afraid : Bool) -> (s : MindSituation) ->
  rankWithModel (stalkEligible True cap contact afraid) s = map Base (rankModes s)
exposedRankingIsTheMindRanking _ _ _ _ = Refl

||| RULE 3 for the candidate: without the capability the ranking is 1.1's.
export
noCapabilityRankingIsTheMindRanking : (ex, contact, afraid : Bool) -> (s : MindSituation) ->
  rankWithModel (stalkEligible ex False contact afraid) s = map Base (rankModes s)
noCapabilityRankingIsTheMindRanking True _ _ _ = Refl
noCapabilityRankingIsTheMindRanking False _ _ _ = Refl

||| Fear removes the candidate: an afraid agent never stalks.
export
fearRemovesStalk : (ex, cap, contact : Bool) -> stalkEligible ex cap contact True = False
fearRemovesStalk True _ _ = Refl
fearRemovesStalk False False _ = Refl
fearRemovesStalk False True False = Refl
fearRemovesStalk False True True = Refl

||| With nothing visible there is nothing to stalk.
export
noContactNoStalk : (ex, cap, afraid : Bool) -> stalkEligible ex cap False afraid = False
noContactNoStalk True _ _ = Refl
noContactNoStalk False False _ = Refl
noContactNoStalk False True _ = Refl

-- The stalker's three moments (healthy, armed, no lead, calm).

||| Far away (3.0): stalk first, then 1.1's advance and the rest.
export
theStalkerFarAway :
  rankWithModel True (MkMind True 196608 False False False 0)
  = [Stalk, Base ModeAdvance, Base ModeReload, Base ModeEvade, Base ModeEngage, Base ModeInvestigate]
theStalkerFarAway = Refl

||| Inside attack range (0.61) but beyond strike range: stalk still outranks engage.
||| This is where the control, with no capability, already engages.
export
theStalkerHoldsItsStrike :
  ( rankWithModel True (MkMind True 40000 False False False 0)
  , rankWithModel False (MkMind True 40000 False False False 0) )
  = ( [Stalk, Base ModeEngage, Base ModeAdvance, Base ModeReload, Base ModeEvade, Base ModeInvestigate]
    , [Base ModeEngage, Base ModeAdvance, Base ModeReload, Base ModeEvade, Base ModeInvestigate] )
theStalkerHoldsItsStrike = Refl

||| Inside strike range (0.46): stalk scores nothing and falls to last; engage is first.
export
theStalkerStrikes :
  rankWithModel True (MkMind True 30000 False False False 0)
  = [Base ModeEngage, Base ModeAdvance, Base ModeReload, Base ModeEvade, Base ModeInvestigate, Stalk]
theStalkerStrikes = Refl

||| The weights, in the order that makes the candidate safe:
|||
|||   * concealment defers the strike (stalk above engage);
|||   * an engagement already begun is not abandoned for a stalk (engage + margin above it);
|||   * inside strike range a stalking incumbent cannot hold (0 + margin below engage);
|||   * a wounded agent flees before it starts a stalk (stalk below flee);
|||   * a stalker always reloads (stalk + margin below reload).
|||
||| NOT claimed: that a stalking INCUMBENT yields to flight. 55705 + 8192 = 63897 is above
||| 58982 + 4096 = 63078, so a stalker that is wounded but not afraid keeps stalking.
||| Once the token is afraid the candidate is gone (`fearRemovesStalk`). Recorded in
||| ADR-0014 as measured-by-theorem rather than by fixture.
export
theStalkWeightIsSafe :
  ( So (Abi.Utility.scoreAttack < Abi.Exposure.utilityStalk)
  , So (Abi.Exposure.utilityStalk < Abi.Utility.scoreAttack + Abi.Modes.stickiness)
  , So (Abi.Modes.stickiness < Abi.Utility.scoreAttack)
  , So (Abi.Exposure.utilityStalk < Abi.Utility.scoreFlee)
  , So (Abi.Exposure.utilityStalk + Abi.Modes.stickiness < Abi.Utility.scoreReload) )
theStalkWeightIsSafe = (Oh, Oh, Oh, Oh, Oh)

||| Strike range is inside attack range, so "engages only inside strike range" is a
||| narrowing of 1.1's engagement, never a widening.
export
strikeInsideAttack : So (Abi.Exposure.strikeRange < 65536)
strikeInsideAttack = Oh

--------------------------------------------------------------------------------
-- The tell
--------------------------------------------------------------------------------

||| CAUGHT: a cue, on a tick that opened unexposed, while the agent stalks.
public export
caughtTell : (cue, exposedBefore, stalking : Bool) -> Bool
caughtTell cue ex stalking = cue && (not ex && stalking)

||| A cue always exposes (under the capability)...
export
aCueExposes : (ex, live, att : Bool) -> exposedAfter True ex live True att = True
aCueExposes _ _ _ = Refl

||| ...so the tick after a CAUGHT opens exposed, and no second CAUGHT can follow until
||| the belief has lapsed: one tell per discovery, never a stutter.
export
noSecondCatchWhileExposed : (cue, stalking : Bool) -> caughtTell cue True stalking = False
noSecondCatchWhileExposed False _ = Refl
noSecondCatchWhileExposed True _ = Refl
