--[[
 * Helper Threads Toolkit
 * (c) 2006 Javier Guerra G.
 * $Id: sched.lua,v 1.11 2006-05-27 00:52:24 jguerra Exp $
--]]

local error, next, unpack = error, next, unpack
local coroutine, table = coroutine, table
local helper = require "helper"

module (arg and arg[1])

local _out_queue = helper.newqueue ()
local _task_co = {}
local _co_queue = {}
local _co_thread = {}
local _name_queue = {}
local _name_threads = {}

---------------------
-- resumes a coroutine
-- is run until blocked again, the new
-- blocking task is added to the queue
---------------------
local function _step (co, task)
	local ok, task2 = coroutine.resume (co, task)
	if not ok then error (task2) end
	
	if task2 and coroutine.status (co) ~= "dead" then
		_task_co [task2] = co
		if helper.state (task2) == "Ready" then
			_co_queue [co]:addtask (task2)
		end
		
	else
		_co_thread [co] = nil
		_co_queue [co] = nil
	end
end


--------------------------------------------------------
-- named_queue (name, n_threads)
--
-- creates a named queue with a bunch of helper threads
-- if the name was already used, the existing queue and
-- threads are released and replaced
-- TODO: close the queue, so it would be disposed
-- after the last thread is done
--------------------------------------------------------
function named_queue (name, n_threads)
	
	_name_queue [name] = nil
	_name_threads [name] = nil
	
	if n_threads <= 0 then return end
	
	_name_queue [name] = helper.newqueue ()
	_name_threads [name] = {}
	
	for i = 1, n_threads do
		table.insert (_name_threads [name], helper.newthread (_name_queue[name], _out_queue))
	end
	
end

---------------------------------------------------------------------------
-- sched.add_thread (f [, name])
--
-- f: function; wrapped in a coroutine and scheduled to run
-- name: any;  all threads with the same name use the same helper thread
--     if nil, false or omitted, it gets it's own helper thread
---------------------------------------------------------------------------
function add_thread (f, name)
	
	local queue, thread
	
	if name then
		assert (_name_queue [name], "unknown queue name")
		queue = _name_queue [name]
		thread = nil
	else
		queue = helper.newqueue ()
		thread = helper.newthread (queue, _out_queue)
		
	end
	
	local co = coroutine.create (function (t) helper.update (t) return f() end)
	
	local task = helper.null ()
	queue:addtask (task)
	
	_task_co [task] = co
	_co_queue [co] = queue
	_co_thread [co] = thread
end

------------------------------------------------------
-- helper.yield (task [, ...])
--
-- wrap task-producing functions with this to
-- yield to other threads.
-- any extra arguments are used at helper.update() time
---------------------------------------------------------
function yield (t, ...)
	return helper.update (coroutine.yield (t), unpack (arg))
end

--------------------------------------
-- helper.run ()
--
-- runs scheduled threads.
-- finishes when all threads are done
--------------------------------------
function run ()
	while next (_task_co) ~= nil do
		local task = _out_queue:wait()
		if task ~= nil then
			local co = _task_co [task]
			
			if helper.state (task) == "Done" then
				_task_co [task] = nil
			end
		
			_step (co, task)
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
