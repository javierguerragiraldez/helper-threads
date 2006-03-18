--[[
 * Helper Threads Toolkit
 * (c) 2006 Javier Guerra G.
 * $Id: sched.lua,v 1.1 2006-03-18 02:10:56 jguerra Exp $
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