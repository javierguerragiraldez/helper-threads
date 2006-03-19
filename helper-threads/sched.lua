--[[
 * Helper Threads Toolkit
 * (c) 2006 Javier Guerra G.
 * $Id: sched.lua,v 1.2 2006-03-19 14:53:19 jguerra Exp $
--]]


require "helper"

module (arg and arg[1])

local _out_queue = helper.newqueue ()
local _task_co = {}
local _co_queue = {}
local _co_thread = {}

function add_thread (f)
	
	local co = coroutine.create (f)
	local tsk = helper.null()
	local queue = helper.newqueue ()
	local thread = helper.newthread (queue, _out_queue)
	
	_task_co [tsk] = co
	_co_queue [co] = queue
	_co_thread [co] = thread
	
	queue:addtask (tsk)
end

function run ()
	while true do
		local task = _out_queue:wait()
		
		local co = _task_co [task]
		_task_co [task] = nil
		
		local ok, task2 = coroutine.resume (co, task)
		
		if task2 and coroutine.status (co) ~= "dead" then
			_task_co [task2] = co
			_co_queue [co]:addtask (task2)
			
		else
			_co_thread [co] = nil
			_co_queue [co] = nil
		end
	end
end

function par_foreach (in_t, f_a, f_b, n_th)
	local out_t = {}
	local in_q = helper.newqueue ()
	local out_q = helper.newqueue ()
	local on_q = {}
	local on_q_n = 0
	local threads = {}
	
	for i = 1, n_th do
		table.insert (threads, helper.newthread (in_q, out_q))
	end
	
	while next (in_t, nil) ~= nil or on_q_n > 0 do
	
		while on_q_n < n_th*2 and next (in_t, nil) ~= nil do
			local k = next (in_t, nil)
			local tsk = f_a (k, in_t [k])
			in_q:addtask (tsk)
			on_q [tsk] = k
			on_q_n = on_q_n +1
			in_t [k] = nil
		end
		
		local tsk = out_q:wait ()
		local k = tsk and on_q [tsk]
		if k then
			out_t [k] = f_b (tsk, k)
			on_q [tsk] = nil
			on_q_n = on_q_n -1
		end
	end
	
	return out_t
end
