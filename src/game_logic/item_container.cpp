/* Copyright (C) 2016, Nikolai Wuttke. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "item_container.hpp"

#include "data/sound_ids.hpp"
#include "engine/base_components.hpp"
#include "engine/collision_checker.hpp"
#include "engine/life_time_components.hpp"
#include "engine/sprite_tools.hpp"
#include "engine/visual_components.hpp"
#include "game_logic/damage_components.hpp"
#include "game_logic/effect_components.hpp"
#include "game_logic/entity_factory.hpp"
#include "game_logic/global_dependencies.hpp"

#include "game_service_provider.hpp"


namespace rigel { namespace game_logic {

using engine::components::Active;
using engine::components::BoundingBox;
using engine::components::Sprite;
using engine::components::WorldPosition;
using game_logic::components::ItemContainer;


ItemContainerSystem::ItemContainerSystem(
  entityx::EntityManager* pEntityManager,
  entityx::EventManager& events
)
  : mpEntityManager(pEntityManager)
{
  events.subscribe<events::ShootableKilled>(*this);
}


void ItemContainerSystem::update(entityx::EntityManager& es) {
  using RS = ItemContainer::ReleaseStyle;

  auto releaseItem = [this](
    entityx::Entity entity,
    const std::vector<ComponentHolder>& containedComponents
  ) {
    auto contents = mpEntityManager->create();
    for (auto& component : containedComponents) {
      component.assignToEntity(contents);
    }

    contents.assign<Active>();
    contents.assign<WorldPosition>(*entity.component<WorldPosition>());

    entity.destroy();
  };

  for (auto& entity : mShotContainersQueue) {
    auto& container = *entity.component<ItemContainer>();

    switch (container.mStyle) {
      case RS::Default:
        releaseItem(entity, container.mContainedComponents);
        break;

      case RS::ItemBox:
        ++container.mFramesElapsed;

        if (container.mFramesElapsed == 1) {
          entity.component<Sprite>()->flashWhite();
        } else if (container.mFramesElapsed == 2) {
          releaseItem(entity, container.mContainedComponents);
        }
        break;

      case RS::NuclearWasteBarrel:
        ++container.mFramesElapsed;

        if (container.mFramesElapsed == 1) {
          entity.component<Sprite>()->flashWhite();
        } else if (container.mFramesElapsed == 2) {
          // Switch to "bulging" state
          ++entity.component<Sprite>()->mFramesToRender[0];
        } else if (container.mFramesElapsed == 3) {
          // At this point, the destruction effects take over
          entity.component<Sprite>()->mShow = false;
        } else if (container.mFramesElapsed == 4) {
          releaseItem(entity, container.mContainedComponents);
        }
        break;
    }
  }

  mShotContainersQueue.erase(
    remove_if(begin(mShotContainersQueue), end(mShotContainersQueue),
      [](const entityx::Entity entity) {
        return !entity.valid();
      }),
    end(mShotContainersQueue));
}


void ItemContainerSystem::receive(const events::ShootableKilled& event) {
  auto entity = event.mEntity;
  if (entity.has_component<ItemContainer>()) {
    // We can't open up the item container immediately, but have to do it
    // in our update() function. This is because the container's contents
    // might be shootable, and this could cause them to be hit by the
    // same projectile as the one that opened the container. By deferring
    // opening the container to our update, the damage infliction update
    // will be finished, so this problem can't occur.
    entity.component<components::Shootable>()->mDestroyWhenKilled = false;
    mShotContainersQueue.emplace_back(entity);
  }
}


namespace behaviors {

void NapalmBomb::update(
  GlobalDependencies& d,
  const bool,
  const bool,
  entityx::Entity entity
) {
  using components::DestructionEffects;
  using State = NapalmBomb::State;

  ++mFramesElapsed;

  const auto& position = *entity.component<WorldPosition>();
  auto& sprite = *entity.component<Sprite>();

  switch (mState) {
    case State::Ticking:
      if (mFramesElapsed >= 25 && mFramesElapsed % 2 == 1) {
        sprite.flashWhite();
      }

      if (mFramesElapsed >= 31) {
        entity.component<DestructionEffects>()->mActivated = true;
        explode(d, entity);
      }
      break;

    case State::SpawningFires:
      if (mFramesElapsed > 10) {
        entity.destroy();
        return;
      }

      if (mFramesElapsed % 2 == 0) {
        const auto step = mFramesElapsed / 2;
        spawnFires(d, position, step);
      }
      break;
  }
}


void NapalmBomb::onKilled(
  GlobalDependencies& d,
  const bool,
  const base::Point<float>&,
  entityx::Entity entity
) {
  explode(d, entity);
}


void NapalmBomb::explode(GlobalDependencies& d, entityx::Entity entity) {
  const auto& position = *entity.component<WorldPosition>();

  d.mpServiceProvider->playSound(data::SoundId::Explosion);
  spawnFires(d, position, 0);

  mState = NapalmBomb::State::SpawningFires;
  mFramesElapsed = 0;
  entity.component<Sprite>()->mShow = false;
  entity.remove<engine::components::MovingBody>();
}


void NapalmBomb::spawnFires(
  GlobalDependencies& d,
  const base::Vector& bombPosition,
  const int step
) {
  using namespace game_logic::components::parameter_aliases;

  auto spawnOneFire = [&, this](const base::Vector& position) {
    const auto canSpawn =
      d.mpCollisionChecker->isOnSolidGround(
        position,
        BoundingBox{{}, {2, 1}});

    if (canSpawn) {
      auto fire = createOneShotSprite(*d.mpEntityFactory, 65, position);
      fire.assign<components::PlayerDamaging>(Damage{1});
      fire.assign<components::DamageInflicting>(
        Damage{1},
        DestroyOnContact{false});
    }
    return canSpawn;
  };

  const auto basePosition = WorldPosition{step + 1, 0};
  if (mCanSpawnLeft) {
    mCanSpawnLeft = spawnOneFire(bombPosition + basePosition * -2);
  }

  if (mCanSpawnRight) {
    mCanSpawnRight = spawnOneFire(bombPosition + basePosition * 2);
  }
}

}}}
