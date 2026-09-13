# Box2D

Unmodified `include/`, `src/`, `LICENSE` and root `CMakeLists.txt` from
https://github.com/erincatto/box2d/tree/v3.1.0 (MIT).

Archive: https://github.com/erincatto/box2d/archive/refs/tags/v3.1.0.zip
SHA256: A48E434033D93B392A58C8664EF18EECD9F20F5EE0F584970DD04107EF88D376

The server builds the collision/query subset with `Server/Box2D/Box2D.vcxproj`.
No physics world, simulation step, global length-unit changes, or downloads at
build time are required. Coordinates passed to Box2D are meters relative to the
map origin. Each query has its own temporary cache; the loaded tree is immutable.
