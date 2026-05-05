<script lang="ts">
    export let favorite: boolean = false;
</script>

<absolutelayout
    style="transform: translate(13, -6) rotate(45)"
    backgroundColor="#f7b500"
    height={20}
    horizontalAlignment="right"
    verticalAlignment="top"
    visibility={favorite ? 'visible' : 'hidden'}
    width={40}></absolutelayout>
