myid = 99999;
move_count = 0;

function set_uid(x)
   myid = x;
end

function event_player_move(player)
   player_x = API_get_x(player);
   player_y = API_get_y(player);
   my_x = API_get_x(myid);
   my_y = API_get_y(myid);
   if (player_x == my_x) then
      if (player_y == my_y) then
        move_count = 0;
        API_SendMessage(myid, player, "HELLO");
      end
   end
end

function event_npc_move(player)
   if (move_count >= 3) then
       API_SendMessage(myid, player, "BYE");
       return false;
   else
       move_count = move_count+1;
       return true;
   end
end