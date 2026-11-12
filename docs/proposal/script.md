# Proposal presentation script

About three minutes (roughly 400 words) for the 11 slides in [swing-to-mine-proposal.pdf](swing-to-mine-proposal.pdf). The speaker split is a suggestion.

## 1. Cover (Oliver)

Hi, we’re Oliver and Eli. Our project is Swing to Mine, a motion controller for Minecraft. This is our plan. Nothing is built yet.

## 2. The idea (Oliver)

You hold an STM32 Discovery board like a sword. Swing it, and your character swings. The board plugs in as a normal USB keyboard and mouse, so there’s nothing to install. A small neural network on the chip recognizes each gesture. And the board is the whole controller.

## 3. How it will work (Oliver)

Here’s how it will work. A gyro and accelerometer read motion a hundred times a second. The model checks the last second of motion every fifty milliseconds. A few frames have to agree before anything fires. Then the board sends the click over USB.

## 4. The gestures (Oliver)

These are the moves. Slash or chop to attack, and keep striking to mine. Punch to place a block. Hop to jump. Twist to swap items. Shake for the inventory. Everything else has to count as nothing.

## 5. Looking and walking (Oliver)

Looking and walking don’t use the model. The gyro works like an air mouse for aiming, and tilting the board walks. The blue button switches between the two. Over to Eli.

## 6. The data (Eli)

We’ll record our own data, like in Lab 2. Four or five people, forty reps of each gesture, plus ten minutes each of just moving around. That “nothing” data is what stops accidental clicks. We’ll test on a person the model has never seen.

## 7. The hard parts (Eli)

It’s harder than Lab 2 for three reasons. The model sees a live stream, not neat clips. Most motion isn’t a gesture. And it has to be fast and fit on the chip.

## 8. Targets (Eli)

Our targets: ninety percent accuracy on a new person, a tenth of a second from motion to input, and at most one accidental gesture a minute. We’ll measure all three on the real board.

## 9. The plan (Eli)

We want it working by Thanksgiving. First, get data off the board. Then a working controller with no model. Then the model on the chip. Then tuning and a demo video. Oliver does the firmware, and I do the recording and training.

## 10. Risks (Eli)

The biggest risk is the model runtime not fitting on the chip, so we have a lighter backup. If we run short on time, we cut gestures and use a real keyboard for walking. The on-chip model and the live demo stay.

## 11. Close (Oliver)

The real test is December eighth: one of you plays Minecraft with it and grins. Questions?
