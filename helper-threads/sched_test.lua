
require "helper"
require "sched"
require "timer"

if true then 

	function de_dos ()
		print ("de_dos")
		
		while true do
			local nada = sched.yield (timer.timer (2))
			print ("dos")
		end
	end
	
	function de_tres ()
		print ("de_tres")
		
		while true do
			local nada = sched.yield (timer.timer (3))
			print ("\ttres")
		end
	end
	
	sched.add_thread (de_dos)
	sched.add_thread (de_tres)
	sched.run ()

else

	local tablesize = 100
	local n_threads = 5

	function delay (k,v)
		print ("delay:", "k", k, "v", v)
		return timer.timer (v)
	end
	function closedelay (tsk,k)
		print ("end:", "k", k, "result", helper.update (tsk))
		return k*2
	end
	
	local in_t = {}
	for i = 1, tablesize do
		math.randomseed (i)
		in_t [i] = math.random (100)/50
	end
	table.foreach (in_t, print)
	local out_t = sched.par_foreach (in_t, delay, closedelay, n_threads)
	table.foreach (out_t, print)

end
