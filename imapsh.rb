#!/usr/bin/ruby

require 'net/imap'

require 'rubygems'
require 'highline/import'


def prompt(str, pfix = "> ")
	print str + pfix
	$stdout.flush
end

def get_password(prompt="Enter Password")
	ask(prompt) {|q| q.echo = false}
end

class IMAPShError < RuntimeError
end

class IMAPSh
	attr_accessor :conn, :srvr, :mbox, :username

	def connect(server)
		begin
			@conn = Net::IMAP.new(server, '993', true)
		rescue SocketError => error
			warn "Failed to connect to #{server}: #{error.message}"
			return 1
		end

		@srvr = server

		return 0
	end

	def disconnect
		if !@conn.nil?
			@conn.disconnect()
			@conn = nil
		end
	end

	def login
		
		prompt("Username", ": ")
		user = $stdin.readline.chomp

		passwd = get_password("Password")

		@conn.authenticate("CRAM-MD5", user, passwd)

		@username = user
	end

	def select(mailbox)
		if @username.nil?
			warn "Must login first"
			return
		end
		
		begin
			@conn.select(mailbox)
		rescue Net::IMAP::NoResponseError => error
			warn "Failed to select #{mailbox}: #{error.message}"
			return
		end
		
		@mbox = mailbox
	end

	def expunge
		if @mbox.nil?
			warn "No mailbox selected"
			return
		end

		@conn.expunge
	end

	def get_prompt
		u = @username.nil? ? "" : "#{@username}@"
		if @srvr.nil?
			return ""
		elsif @mbox.nil?
			return "#{u}#{@srvr}"
		else
			return "#{u}#{@srvr}:#{@mbox}"
		end
	end
end

running = true
imapsh = IMAPSh.new()
while running
	
	prompt(imapsh.get_prompt)

	begin
		line = $stdin.readline
	rescue EOFError
		puts "Good bye!"
		exit 0
	end

	cmds = line.split(" ")
	if cmds.length == 0
		next
	end

	if cmds[0] == "quit"
		puts "Good bye!"
		exit 0
	elsif cmds[0] == "connect"
		if cmds.length != 2
			warn "Usage: connect <server>"
			next
		end

		imapsh.connect(cmds[1])
	elsif cmds[0] == "capability"
		caps = imap.capability
		puts caps.inspect
	elsif cmds[0] == "login"
		imapsh.login
	elsif cmds[0] == "select"
		if cmds.length != 2
			warn "Usage: select <mailbox>"
			next
		end

		imapsh.select(cmds[1])
	elsif cmds[0] == "expunge"
		imapsh.expunge
	else
		warn "Unknown command: #{cmds[0]}"
	end

	#puts "line: " + cmds.inspect
end
