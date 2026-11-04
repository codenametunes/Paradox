# Paradox

### Minecraft, written in C.

Paradox is an independent Minecraft-inspired voxel sandbox written in **C**. It aims to recreate the core Minecraft experience, featuring procedural terrain, block interaction, lighting, inventory, different game modes, and more.

It doesn't have everything Minecraft has, but it has enough to feel like Minecraft.

## Features

- Procedurally generated voxel terrain
- Chunk-based world rendering
- Block placement and destruction
- Multiple block types
- Lighting system
- Fluid blocks
- Inventory and hotbar
- First-person and third-person perspectives
- Two game modes
- Day/night cycle and sky rendering

## Controls

- WASD - Movement
- Mouse - Look around
- Space - Jump
- Left Mouse Button - Break block
- Right Mouse Button - Place block
- F4 - Toggle wireframe view
- F5 - Change camera perspective
- F10 - Change game mode
- F11 - Toggle fullscreen
- E - Open inventory
- ESC - Pause menu

*Movement and interaction controls follow the familiar Minecraft layout.*

## Technical Notes

Paradox is written in C, but not every system was developed entirely from scratch.

A considerable amount of the game's underlying behavior references Minecraft's **deobfuscated Java source code**, particularly:

- Physics and collision behavior
- Player movement and movement mechanics
- Entity and world scaling
- Various calculations and miscellaneous gameplay mechanics

These references help reproduce Minecraft's familiar behavior while implementing the actual game systems in C.

## Known Issues

- Character skin rendering is currently broken.
- Fluid textures are not mapped correctly.

These issues are known and may be addressed in future updates.

## Planned Features

- Biome generation
- Structures and world generation
- Improved water generation
- Passive mobs
- Improved character skin rendering
- Correct fluid texture mapping

## Built With

- C
- OpenGL
- GLFW
- GLAD
- CMake
- FreeType

## License

This project is licensed under the **MIT License**. See [LICENSE](LICENSE) for details.

## Disclaimer

Paradox is an independent, unofficial Minecraft-inspired project. It is not affiliated with, endorsed by, or associated with Mojang Studios or Microsoft.

Minecraft is a trademark of Mojang Studios.
