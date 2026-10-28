#include "Item.hpp"
namespace th10 {
// 0x41b8e0. This includes the original upper-edge indicator alpha and the
// sprite comparison/binding index difference; neither is normalized here.
void Item::draw(ItemDrawEnvironment& env){
    if(!state)return;
    auto& vm=animation;vm.script_position.x=Scalar::add(position.x,224.0f);
    vm.script_position.y=Scalar::add(position.y,16.0f);vm.script_position.z=position.z;
    if(number(vm.script_position.y)<number(8.0f)){
        const auto delta=number(vm.script_position.y)-number(8.0f);vm.script_position.y=24.0f;
        const u8 alpha=number(32.0f)<delta||number(32.0f)==delta?255:static_cast<u8>((delta*number(.03125f)*number(255.0f)).truncate_int());
        vm.color=(vm.color&0x00ffffff)|(static_cast<u32>(alpha)<<24);
        if(vm.sprite_index!=wrapping_add(sprite_kind,0x161))env.bind_item_sprite(vm,wrapping_add(sprite_kind,0x160));
    }else if(vm.sprite_index!=wrapping_add(sprite_kind,0x158)){
        env.bind_item_sprite(vm,wrapping_add(sprite_kind,0x157));vm.color|=0xff000000;
    }
    env.draw_animation(vm);
}
i32 ItemManager::draw(ItemDrawEnvironment& env){for(auto& item:regular)item.draw(env);for(auto& item:faith)item.draw(env);return 1;}
}
