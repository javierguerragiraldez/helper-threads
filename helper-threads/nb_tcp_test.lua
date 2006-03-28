
require "helper"
require "sched"
require "nb_tcp"

function do_listen ()
	local srv = assert (nb_tcp.newserver (3000))
	while true do
		local conn = assert (sched.yield (srv:accept ()))
		print ("connected!")
		sched.add_thread (function ()
			handle_client (conn)
		end)
	end
end

function handle_client (conn)
	print ("conectado")
	repeat
		local ln = assert (sched.yield (conn:read ("*l")))
		assert (sched.yield (conn:write ("=>"..ln.."\n")))
	until ln == "quit"
	print ("close..")
end

sched.add_thread (do_listen)
sched.run()