inputs=inputs/*
for i in paper/$inputs
do
  ./run_sssp.sh $i
done
