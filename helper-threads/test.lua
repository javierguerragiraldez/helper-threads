--
-- Helper Threads Toolkit
-- (c) 2006 Javier Guerra G.
-- $Id: test.lua,v 1.4 2006-03-13 22:10:23 jguerra Exp $
--

require "helper"
require "timer"


q1=helper.newqueue()
q2=helper.newqueue()
print ("queues:", q1, q2)

th=helper.newthread (q1, q2)
print ("thread:", th)
th=helper.newthread (q1, q2)
print ("thread:", th)

t1=timer.timer(10)
print ("t1 (10 sec):", t1)
print ("state:", helper.state (t1))

t2=timer.timer(2)
print ("t2 (2 sec):", t2)
print ("state:", helper.state (t2))

--helper.finish(tsk)

q1:addtask (t1)
q1:addtask (t2)

tx=q2:wait()
print ("tx:", tx);
print ("state:", helper.state (tx))
print ("t1:", helper.state (t1), "t2:", helper.state (t2))
helper.update (tx)

tx=q2:wait()
print ("tx:", tx);
print ("state:", helper.state (tx))
print ("t1:", helper.state (t1), "t2:", helper.state (t2))
helper.update (tx)

tk = timer.ticks (1.5)
print ("tk:", tk)
q1:addtask (tk)
while true do
	local nexttime
	if math.random (10) <= 1 then
		nexttime = math.random()
		print ("random:", nexttime)
	else
		nexttime = nil
	end
	
	tx = q2:wait ()
	print ("tx:", tx, helper.update (tx, nexttime))
end
