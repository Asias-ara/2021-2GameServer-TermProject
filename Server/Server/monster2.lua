-- agro����

myid = 99999;
lv = 20;
name = "Project";
hp = 900;

function set_uid(x)
   myid = x;
   return lv, hp, name;
end

function event_player_move(player)
   player_x = API_get_x(player);
   player_y = API_get_y(player);
   my_x = API_get_x(myid);
   my_y = API_get_y(myid);
   if (player_x == my_x) then
      if (player_y == my_y) then
        
      end
   end
end