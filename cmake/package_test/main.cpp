#include <zecs/world.h>

struct Position
{
    int x = 0;
    int y = 0;
};

int main()
{
    zecs::World world;
    const zecs::Entity entity = world.entity_add();
    world.comp_add<Position>(entity, 3, 4);

    int sum = 0;
    for (const zecs::Entity e : world.view_comp_group<Position>())
        sum += world.comp_get<Position>(e)->x + world.comp_get<Position>(e)->y;

    return (sum == 7) ? 0 : 1;
}
