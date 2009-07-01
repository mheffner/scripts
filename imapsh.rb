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

class IMAPShSearches
	attr_accessor :searches, :seqno

	def initialize
		@seqno = 1
		@searches = {}
	end

	def print
		puts "Saved searches:"
		@searches.keys.each { |k|
			puts "#{k}"
		}
	end

	def add(result)
		seq = @seqno

		@searches[seq] = result
		@seqno += 1

		return seq
	end

	def delete(seq)
		seq = Integer(seq)

		if !@searches.exists(seq)
			warn "No saved search at #{seq}"
		else
			@searches.delete(seq)
		end
	end

	def get(seq)
		seq = Integer(seq)
		return @searches[seq]
	end

	def exist(seq)
		seq = Integer(seq)
		return @searches.key?(seq)
	end
end

class IMAPSh
	attr_accessor :conn, :srvr, :mbox, :username, :searches

	def initialize
		@searches = IMAPShSearches.new()
	end

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

		begin
			@conn.authenticate("CRAM-MD5", user, passwd)
		rescue Net::IMAP::NoResponseError => error
			warn "Failed to login: #{error.message}"
			return
		end

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

	def search(keys)
		if @mbox.nil?
			warn "No mailbox selected"
			return
		end

		begin
			result = @conn.search(keys)
		rescue Net::IMAP::BadResponseError => error
			warn "Bad search '#{keys.join(" ")}': #{error.message}"
			return
		end

		seq = @searches.add(result)

		puts "Search results saved at #{seq}"
	end

	def sort(sort_keys, search_keys = nil)
		if @mbox.nil?
			warn "No mailbox selected"
			return
		end

		# XXX: make lists
		sort_keys = [sort_keys]
		if search_keys.nil?
			search_keys = ["ALL"]
		else
			search_keys = [search_keys]
		end

		begin
			result = @conn.sort(sort_keys, search_keys, "US-ASCII")
		rescue Net::IMAP::BadResponseError => error
			warn "Bad sort '#{sort_keys.join(" ")}': #{error.message}"
			return
		end

		seq = @searches.add(result)
		puts "Search result saved at #{seq}"
	end

	def list(seq = nil)
		if !seq.nil? and !@searches.exist(seq)
			warn "Search sequence #{seq} does not exist"
			return
		end

		if seq.nil?
			@searches.print
		else
			s = @searches.get(seq)

			puts "Search #{seq} matched: #{s.join(",")}"
		end
	end

	def uniq(seq)
		if !@searches.exist(seq)
			warn "Search sequence #{seq} does not exist"
			return
		end

		set = @searches.get(seq)
		if set.empty? or set.length == 1
			@searches.delete(seq)
			return
		end

		curr_id = nil
		next_id = nil
		for i in (0..(set.length - 2))
			if curr_id.nil?
				res = @conn.fetch(set[i],
						  "BODY[HEADER.FIELDS (MESSAGE-ID)]")
				curr_id = res[0].attr["BODY[HEADER.FIELDS (MESSAGE-ID)]"]
			end

			res = @conn.fetch(set[i + 1],
					  "BODY[HEADER.FIELDS (MESSAGE-ID)]")
			next_id = res[0].attr["BODY[HEADER.FIELDS (MESSAGE-ID)]"]

			#puts "current id: #{curr_id}, next_id: #{next_id}"

			if curr_id == next_id
				puts "Deleting message id #{set[i]} as a dup of #{set[i + 1]}"
				# XXX do delete here!
			end

			curr_id = next_id
			next_id = nil
		end			
	end

	def delete(seq)
		if !@searches.exist(seq)
			warn "Search sequence #{seq} does not exist"
			return
		end

		@searches.delete(seq)

		puts "Deleted saved search #{seq}"
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
	elsif cmds[0] == "search"
		if cmds.length < 2
			warn "Usage: search <keys>"
			next
		end

		imapsh.search(cmds[1, cmds.length - 1])

	elsif cmds[0] == "sort"
		if cmds.length < 2
			warn "Usage: sort <sort key> <search key>"
			next
		end

		if cmds.length == 2
			imapsh.sort(cmds[1])
		else
			sort = cmds.shift
			imapsh.sort(sort, cmds)
		end
	elsif cmds[0] == "list"
		if cmds.length > 2
			warn "Usage: list [<set>]"
			next
		end

		if cmds.length == 2
			imapsh.list(cmds[1])
		else
			imapsh.list
		end
	elsif cmds[0] == "uniq"
		if cmds.length != 2
			warn "Usage: uniq <set>"
			next
		end

		imapsh.uniq(cmds[1])
	elsif cmds[0] == "delete"
		if cmds.length != 2
			warn "Usage: delete <set>"
			next
		end

		imapsh.delete(cmds[1])
	elsif cmds[0] == "expunge"
		imapsh.expunge
	else
		warn "Unknown command: #{cmds[0]}"
	end

	#puts "line: " + cmds.inspect
end
