
require "helper"
require "sched"
require "nb_tcp"

local function handle_client (conn)
	local ln
	repeat
		ln = assert (sched.yield (conn:read ("*l")))
		assert (sched.yield (conn:write ("=>"..ln.."\n")))
		print (ln)
	until ln == "quit"
	conn:close()
end

local function do_listen ()
	sched.add_helpers ("listeners", 4)
	local srv = assert (nb_tcp.newserver (8080))
	while true do
		local conn = assert (sched.yield (srv:accept ()))
		sched.add_thread (function ()
			handle_client (conn)
		end, "listeners")
	end
end

sched.add_thread (do_listen)
sched.run()

