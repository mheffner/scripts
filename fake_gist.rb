#!/usr/bin/env ruby

require 'tempfile'

begin
  file = ARGV.shift
  break unless file
  f = File.open(file, "r")
  tmp = Tempfile.new("fakegist")
  f.readlines.each do |l|
    m = l.match(/^[ ]*gist:[ ]*([0-9][0-9]*)[ ]*$/)
    unless m
      tmp << l
      next
    end

    gist = %x{make_pre_tag.pl #{m[1]}}

    tmp << gist
  end

  path = tmp.path
  f.close
  tmp.close
  system("mv #{path} #{file}")
end while true
