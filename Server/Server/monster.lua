-- Peace몬스터

my_id = 99999;
my_lv = 10;
my_name = "Homework";
my_hp = 250;
my_x = 0;
my_y = 0;

function set_uid(id, x, y)
   my_id = id;
   my_x = x;
   my_y = y;
   return my_lv, my_hp, my_name;
end

function event_npc_move(player)
   player_x = API_get_x(player);
   player_y = API_get_y(player);
   x = API_get_x(myid);
   y = API_get_y(myid);
   if (math.abs(my_x - x) > 10) or (math.abs(my_y - y) > 10) then
      return false;
   else
      -- 쫒아가는 것은 C로 짠다
      return true;
   end
end

function return_my_position()
   x = API_get_x(myid);
   y = API_get_y(myid);
   if(my_x ~= x) then
      if(my_x >= x) then
         x = x+1;
         return x, y, true;
      else
         x = x-1;
         return x, y, true;
      end
   elseif (my_y ~= y) then
      if(my_y >= y) then
         y = y+1;
         return x, y, true;
      else
         y = y-1;
         return x, y, true;
      end
   else
      return x, y, false;
   end
end