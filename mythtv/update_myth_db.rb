#!/usr/bin/ruby

require 'mysql'

dbhost = "nagini"
dbuser = "mythtv"
dbpass = "mythtv"
db = "mythconverg"

def freq_defaults(srcid, freq)
  return sprintf("(mplexid, sourceid, frequency, symbolrate, polarity, modulation, hierarchy, sistandard) values (NULL, '%d', '%d', '0', 'v', 'qam_256',  'a', 'atsc')", srcid, freq)
end

#
# Parse options
#

if ARGV.length != 1
  puts "Usage: update_myth_db.rb <path to mplayer channels.conf>"
  exit 1
end

begin
  chanconf = File.new(ARGV[0], "r")
rescue
  puts "Failed to open file: #{ARGV[0]}"
  exit 1
end

begin
  conn = Mysql.new(dbhost, dbuser, dbpass, db)
rescue
  puts "Failed to connect to DB"
  exit 1
end

# Determine source ID
r = conn.query(
               """
               select cardinput.sourceid from cardinput join capturecard
               on (cardinput.cardid = capturecard.cardid) where
               capturecard.cardtype = 'DVB'
""")

sourceid = r.fetch_hash['sourceid']

now = Time.now

chanconf.readlines.each { |l|
  #puts "l: #{l}"

  m = l.match(/^([^:]+):([0-9]+):.*:([0-9]+)$/)
  callsign = m[1]
  freq = m[2]
  srvid = m[3]
  #printf "callsign: %s, freq: %d, srvid: %d\n", callsign, freq, srvid

  # Find the frequency
  r = conn.query("select mplexid from dtv_multiplex where frequency = '#{freq}'")
  if r.num_rows == 0
    puts "Adding Frequency entry for #{callsign}"
    r = conn.query("insert into dtv_multiplex #{freq_defaults(sourceid, freq)}")

    # Now select to find
    r = conn.query("select mplexid from dtv_multiplex where frequency = '#{freq}'")
  end
  exit 1 if r.num_rows != 1

  mplexid = r.fetch_hash['mplexid']

  # Now find channel
  r = conn.query("select * from channel where sourceid = '#{sourceid}' and callsign = '#{callsign}'")
  if r.num_rows == 1
    # Update it
    puts "Updating channel #{callsign}"
    chanid = r.fetch_hash['chanid']

    s = "update channel set mplexid = '#{mplexid}', serviceid = '#{srvid}' where chanid = '#{chanid}'"
    r = conn.query(s)
  else
    begin
      print "Create #{callsign}? [y/n]:"
      choice = STDIN.gets.chomp.upcase
    end while choice != "Y" && choice != "N"

    next if choice != "Y"

    print "Enter channel number: "
    channum = STDIN.gets.chomp

    print "Enter XML TV ID: "
    xmlid = STDIN.gets.chomp

    begin
      print "Create #{callsign}, channel num #{channum}, xmlid: #{xmlid}? [y/n]:"
      choice = STDIN.gets.chomp.upcase
    end while choice != "Y" && choice != "N"

    next if choice != "Y"

    # Weak: no auto increment
    r = conn.query("select max(chanid) from channel")

    newid = Integer(r.fetch_hash['max(chanid)']) + 1

    cols = {
      "chanid" => newid,
      "channum" => channum,
      "sourceid" => sourceid,
      "callsign" => callsign,
      "freqid" => "",
      "name" => callsign,
      "finetune" => "0",
      "videofilters" => "",
      "xmltvid" => xmlid,
      "outputfilters" => "",
      "mplexid" => mplexid,
      "serviceid" => srvid,
      "last_record" => 0,
      "default_authority" => "",
    }

    names = []
    values = []
    cols.each { |k, v|
      names.push(k)
      values.push("'#{v}'")
    }

    s = "insert into channel (#{names.join(",")}) values (#{values.join(",")})"
    conn.query(s)
    puts "Inserted #{callsign}"
  end
}
