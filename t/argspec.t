
require HTML::Parser;

$decl = '<!ENTITY nbsp   CDATA "&#160;" -- no-break space -->';
$com1 = '<!-- Comment -->';
$com2 = '<!-- Comment -- -- Comment -->';
$start = '<a href="foo">';
$end = '</a>';
$empty = "<IMG SRC='foo'/>";
$proc = '<? something completely different ?>';

my $argspec = join ',',
    qw( self offset length
	event tagname token0
	text
	is_cdata dtext
	tokens
	tokenpos
	attr
	attrseq );

my @result = ();
my $p = HTML::Parser -> new(default_h => [\@result, $argspec],
			    strict_comment => 1, xml_mode => 1);

@tests =
    ( # string, expected results
      $decl  => [[$p, 0, 52, 'declaration', 'ENTITY', 'ENTITY',
		 '<!ENTITY nbsp   CDATA "&#160;" -- no-break space -->',
		 undef, undef,
	       ['ENTITY', 'nbsp', 'CDATA', '"&#160;"', '-- no-break space --'],
		 [2, 6, 9, 4, 16, 5, 22, 8, 31, 20],
		 undef, undef ]],
      $com1  => [[$p, 52, 16, 'comment', ' Comment ', ' Comment ',
		 '<!-- Comment -->', 
		 undef, undef,
		 [' Comment '],
		 [4, 9],
		 undef, undef ]],
      $com2  => [[$p, 68, 30, 'comment', ' Comment ', ' Comment ',
		 '<!-- Comment -- -- Comment -->',
		 undef, undef,
		 [' Comment ', ' Comment '],
		 [4, 9, 18, 9],
		 undef, undef ]],
      $start => [[$p, 98, 14, 'start', 'a', 'a',
		 '<a href="foo">', 
		 undef, undef,
		 ['a', 'href', '"foo"'],
		 [1, 1, 3, 4, 8, 5],
		 {'href', 'foo'}, ['href'] ]],
      $end   => [[$p, 112, 4, 'end', 'a', 'a',
		 '</a>',
		 undef, undef,
		 ['a'],
		 [2, 1],
		 undef, undef ]],
      $empty => [[$p, 116, 16, 'start', 'IMG', 'IMG',
		  "<IMG SRC='foo'/>",
		  undef, undef,
		  ['IMG', 'SRC', "'foo'"],
		  [1, 3, 5, 3, 9, 5],
		  {'SRC', 'foo'}, ['SRC'] ],
		 [$p, 132, 0, 'end', 'IMG', 'IMG',
		  '',
		  undef, undef,
		  ['IMG'],
		  undef,
		  undef, undef ],
		 ],
       $proc  => [[$p, 132, 36, 'process', ' something completely different ',
		  ' something completely different ',
		  '<? something completely different ?>',
		  undef, undef,
		  [' something completely different '],
		  [2, 32],
		  undef, undef ]],
      "$end\n$end"   => [[$p, 168, 4, 'end', 'a', 'a',
			  '</a>',
			  undef, undef,
			  ['a'],
			  [2, 1],
			  undef, undef],
			 [$p, 172, 1, 'text', undef, undef,
			  "\n",
			  '', "\n",
			  undef,
			  undef,
			  undef, undef],
			 [$p, 173, 4, 'end', 'a', 'a',
			  '</a>',
			  undef, undef,
			  ['a'],
			  [2, 1],
			  undef, undef ]],
      );
my $n = @tests / 2;
print "1..$n\n";

sub string_tag {
    my (@pieces) = @_;
    my $part;
    foreach $part ( @pieces ) {
	if (!defined $part) {
	    $part = 'undef';
	}
	elsif (!ref $part) {
	    $part = "'$part'" if $part !~ /^\d+$/;
	}
	elsif ('ARRAY' eq ref $part ) {
	    $part = '[' . join(', ', string_tag(@$part)) . ']';
	}
	elsif ('HASH' eq ref $part ) {
	    $part = '{' . join(',', string_tag(%$part)) . '}';
	}
	else {
	    $part = '<' . ref($part) . '>';
	}
    }
    return join(", ", @pieces );
}

my $i = 0;


my ($got, $want);
while (@tests) {
    ($html, $expected) = splice @tests, 0, 2;
    ++$i;

    print "-" x 50, " $i\n";
    print "$html\n";
    print "-" x 50, " $i\n";

    @result = ();
    $p->parse($html)->eof;

    # Compare results for each element expected
    foreach (@$expected) {
	$want = string_tag($_);
	$got = string_tag(shift @result);
	print "          $got\n";
	if ($want ne $got) {
	    print "Expected: $want\n";
	    print( "not " );
	    last;
	}
    }

    print "ok $i\n";
}
