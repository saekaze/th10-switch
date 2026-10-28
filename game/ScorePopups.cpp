#include "ScorePopups.hpp"
namespace th10 {
void ScorePopups::initialize(ScorePopups** current) noexcept {std::memset(this,0,sizeof(*this));flags=2;*current=this;}
i32 ScorePopups::start(ScorePopupEnvironment& env){animation_file=*env.effects;update_entry=(*env.chain)->add(env.update_callback,this,15,false,false,*env.callbacks);draw_entry=(*env.chain)->add(env.draw_callback,this,35,true,false,*env.callbacks);auto* file=animation_file;animation.initialize();animation.animation_file=file;file->bind_sprite(animation,196);return 0;}
void ScorePopups::shutdown(ScorePopupEnvironment& env){(*env.chain)->remove_locked(update_entry,*env.callbacks);(*env.chain)->remove_locked(draw_entry,*env.callbacks);*env.current=nullptr;if(animation.geometry)env.free_bytes(animation.geometry);animation.geometry=nullptr;}
ScorePopups* ScorePopups::create(ScorePopupEnvironment& env){auto* popups=env.allocate();if(!popups)return nullptr;popups->initialize(env.current);if(popups->start(env)){popups->shutdown(env);env.delete_object(popups);return nullptr;}return popups;}
// 0x42b9c0. Only 720 entries participate in allocation, although update/draw
// visit all 723 entries present in the original object.
void ScorePopups::spawn(i32 score,const Vec3& position,u32 color,const float* rate) noexcept {if(cursor>=720)cursor=0;auto& popup=pool[cursor];popup.active=1;u8 length=0;if(score<0)popup.digits[length++]=10;else if(!score)popup.digits[length++]=0;else while(score){popup.digits[length++]=score%10;score/=10;}popup.length=length;popup.color=color;if(!(popup.timer_flags&1)){popup.elapsed.rate=rate;popup.timer_flags|=1;}popup.elapsed.initialize(-1);popup.position=position;cursor=wrapping_add(cursor,1);}
i32 ScorePopups::update(const float* rate) noexcept {for(auto& popup:pool)if(popup.active){popup.position.y=(number(popup.position.y)-number(*rate)*number(.5f)).to_float();popup.elapsed.tick();if(popup.elapsed.current>60)popup.active=0;}return 1;}
// 0x42b780. Close popups are translucent; the final frames choose alternate
// digit sprites. Horizontal position advances even when submission is clipped.
i32 ScorePopups::draw(ScorePopupEnvironment& env){
    if(!(*env.display_flags&4)&&*env.fog){env.flush();*env.fog=0;env.disable_fog();}
    auto* player=*env.player;
    for(const auto& popup:pool){if(!popup.active)continue;const float spacing=popup.elapsed.current<8?Scalar::div(8,popup.elapsed.fractional):8;
        animation.position.x=(number(popup.position.x)-Extended::from_int(popup.length)*number(spacing)*number(.5f)+number(224)).to_float();animation.position.y=Scalar::add(popup.position.y,16);animation.color=popup.color;
        const auto dx=number(player->position.x)-number(popup.position.x),dy=number(player->position.y)-number(popup.position.y);const i32 distance=(dy*dy+dx*dx).truncate_int();const u8 alpha=distance>4096?208:distance<=1024?80:((distance-1024)*128)/3072+80;
        for(i32 i=popup.length-1;i>=0;i--){const auto digit=popup.digits[i];const i32 first=popup.elapsed.current<52||digit==10?196:popup.elapsed.current<56?207:217;animation.sprite=&animation_file->sprites[first+digit];animation.color=(animation.color&0xffffff)|(static_cast<u32>(alpha)<<24);animation.sprite_size.x=animation.sprite->width;animation.flags|=8;env.draw_animation(animation);animation.position.x=Scalar::add(spacing,animation.position.x);}
        if(popup.length)player=*env.player;
    }return 1;
}
}
