# Activity 1

### One Sender, One Receiver

Sender has ID `0x200`.

<img width="2880" height="2160" alt="image" src="https://github.com/user-attachments/assets/4e72e0be-3824-4539-bdd1-ec55c93016aa" />

Console output from receiver:

<img width="985" height="28" alt="image" src="https://github.com/user-attachments/assets/66089c2a-f746-49aa-a51c-9b10202b05cf" />

### Two Senders

In this example, two senders are on the bus with IDs `0x10` and `0x20`.

The difference in voltage levels observed from sender `0x10` is likely due to loose breadboard wiring.

Interestingly, this difference in CANH voltages allows easy identification of the `ACK` bit.

<img width="2880" height="2160" alt="image" src="https://github.com/user-attachments/assets/2d2fc143-00cb-490d-89d9-0fb04c50f3c0" />

# Activity 2

### Two Senders, One Babbling

here we see the `0x10` node babbling (and the diffeerent-voltage-level ACK bit), preventing `0x20` from sending frames.

<img width="2880" height="2160" alt="image" src="https://github.com/user-attachments/assets/155f686b-da9c-4313-b935-49d163434297" />
