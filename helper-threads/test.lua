require "helper"
timer = require "timer"


q1=helper.newqueue()
q2=helper.newqueue()
print ("queues:", q1, q2)

th=helper.newthread (q1, q2)
print ("thread:", th)
th=helper.newthread (q1, q2)
print ("thread:", th)

t1=timer(10)
print ("t1 (10 sec):", t1)
print ("state:", helper.state (t1))

t2=timer(2)
print ("t2 (2 sec):", t2)
print ("state:", helper.state (t2))

--helper.finish(tsk)

q1:addtask (t1)
q1:addtask (t2)

tx=q2:wait()
print ("tx:", tx);
print ("state:", helper.state (tx))
print ("t1:", helper.state (t1), "t2:", helper.state (t2))
helper.finish (tx)

tx=q2:wait()
print ("tx:", tx);
print ("state:", helper.state (tx))
print ("t1:", helper.state (t1), "t2:", helper.state (t2))
helper.finish (tx)

