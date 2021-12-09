-- Boss 몬스터

my_id = 99999;
my_lv = 30;
my_name = "ZELDA";
my_hp = 3000;
my_x = 1000;
my_y = 1000;
skill_attemp = 0;

function set_uid(id)
   my_id = id;
   return my_lv, my_hp, my_name, my_x, my_y;
end

function event_npc_move(player)
   player_x = API_get_x(player);
   player_y = API_get_y(player);
   x = API_get_x(my_id);
   y = API_get_y(my_id);
   if (math.abs(my_x - x) > 10) or (math.abs(my_y - y) > 10) then
      return false;
   else
      -- 쫒아가는 것은 C로 짠다
      return true;
   end
end

function return_my_position()
   return my_x, my_y;
end

function attack_range(player)
   player_x = API_get_x(player);
   player_y = API_get_y(player);
   x = API_get_x(my_id);
   y = API_get_y(my_id);
   if (x == player_x) then
      if (player_y <= (y+1)) then
         if((y-1) <= player_y) then
            return true;
         else
            return false;
         end
      else 
         return false;
      end
   elseif (y == player_y) then
      if (player_x <= (x+1)) then
         if((x-1) <= player_x) then
            return true;
         else
            return false;
         end
      else 
         return false;
      end
   end
end

function skill_pattern()
   mon_hp = API_get_hp(my_id);
   if (mon_hp <= 2000) then
       return 1;
   end
   
   if (mon_hp <= 1000) then
       return  2;
   end

   return 0;
end

function first_skill(skill_type)
    
end