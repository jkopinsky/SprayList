# USAGE: ./run.sh var
#  var: print statistics for indicated variable (default #txs)
#       need complete enough name for grep to find unambiguously (e.g. '#txs' works, 'txs' doesn't)


if [ -z $1 ]
then
input="input/soc-LiveJournal1.txt"
else
input=$1
fi

echo $1

bin=bin/sssp
var=duration

num_procs=(1 2 4 8 10 16 20 21 32 40 60 64 80)
#num_procs=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20)
#num_procs=(8 16 32 64)

echo "#procs,spray unweighted,lotan_shavit unweighted,linden unweighted,spray weighted,lotan_shavit weighted,linden weighted,spray bimodal,lotan_shavit bimodal,linden bimodal"

for i in "${num_procs[@]}"
#for i in {1..80}
do
  spray_u[$i]=`$bin -i $input -o out.txt -n $i -l -m 2000000 | grep $var | grep '(?<= )[0-9]+\.?[0-9]*' -Po`
  lotan_shavit_u[$i]=`$bin -i $input -o out.txt -n $i -p -m 2000000 | grep $var | grep '(?<= )[0-9]+\.?[0-9]*' -Po`
  linden_u[$i]=`$bin -i $input -o out.txt -n $i -L -m 2000000 | grep $var | grep '(?<= )[0-9]+\.?[0-9]+' -Po`
  spray_w[$i]=`$bin -i $input -o out.txt -n $i -l -w -m 2000000 | grep $var | grep '(?<= )[0-9]+\.?[0-9]*' -Po`
  lotan_shavit_w[$i]=`$bin -i $input -o out.txt -n $i -p -w -m 2000000 | grep $var | grep '(?<= )[0-9]+\.?[0-9]*' -Po`
  linden_w[$i]=`$bin -i $input -o out.txt -n $i -L -w -m 2000000 | grep $var | grep '(?<= )[0-9]+\.?[0-9]+' -Po`
  spray_b[$i]=`$bin -i $input -o out.txt -n $i -l -b -m 2000000 | grep $var | grep '(?<= )[0-9]+\.?[0-9]*' -Po`
  lotan_shavit_b[$i]=`$bin -i $input -o out.txt -n $i -p -b -m 2000000 | grep $var | grep '(?<= )[0-9]+\.?[0-9]*' -Po`
  linden_b[$i]=`$bin -i $input -o out.txt -n $i -L -b -m 2000000 | grep $var | grep '(?<= )[0-9]+\.?[0-9]+' -Po`

  echo $i,${spray_u[$i]},${lotan_shavit_u[$i]},${linden_u[$i]},${spray_w[$i]},${lotan_shavit_w[$i]},${linden_w[$i]},${spray_b[$i]},${lotan_shavit_b[$i]},${linden_b[$i]}
done


