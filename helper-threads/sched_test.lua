
require "helper"
require "sched"
require "timer"

function de_dos (t)
	helper.update (t)
	
	print ("de_dos")
	
	while true do
		local nada = helper.update (coroutine.yield (timer.timer (2)))
		print ("dos")
	end
end

function de_tres (t)
	helper.update (t)
	
	print ("de_tres")
	
	while true do
		local nada = helper.update (coroutine.yield (timer.timer (3)))
		print ("\ttres")
	end
end

sched.add_thread (de_dos)
sched.add_thread (de_tres)
sched.run ()