Angel Mendez
asm2265

Part 1: SRNODE

The SR node emulator appears to work correctly but might need further testing for any bugs to be safe.

The SR node emulator uses a few buffers that are used differently depending on the mode (input buffer from stdin and the buffer representing the window)
This was necessary to make the program work with just the one command for both the receiver and sender processes.
Upon completion, the sender sends sequence number negative 1 to the receiver to indicate that all packets have been processed on the sender end. This lets the
receiver know that no more packets will be received, so it prints summary and exits.


Instructions to run srnode emulator:
make
./srnode <self-port> <peer-port> <window-size> [ -d <value-of-n> | -p <value-of-p> ]

The only valid command is send, to be used on the sender node.

Part 2: DVNODE

Right now not completely working. Appropriately finds and sends messages to all nodes. However, there is a bug causing excessive subtraction when finding shortest path.